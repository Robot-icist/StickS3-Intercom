"""Desktop prompts for interactive companion actions."""

from __future__ import annotations

import json
import os
import re
import time
from pathlib import Path
from typing import Sequence


def _session_dir() -> Path | None:
    value = os.environ.get("STICKS3_INTERACTION_DIR")
    return Path(value) if value else None


def _activate_prompt(
    kind: str,
    session_dir: Path | None = None,
) -> Path | None:
    session_dir = session_dir or _session_dir()
    if session_dir is None:
        return None

    session_dir.mkdir(parents=True, exist_ok=True)
    state_path = session_dir / "active_prompt.json"
    temporary_path = session_dir / "active_prompt.tmp"
    temporary_path.write_text(
        json.dumps({"kind": kind, "started_at": time.time()}),
        encoding="utf-8",
    )
    temporary_path.replace(state_path)
    return state_path


def _deactivate_prompt(state_path: Path | None) -> None:
    if state_path is not None:
        state_path.unlink(missing_ok=True)


def _normalize(value: str) -> str:
    value = re.sub(r"(?<=[a-z0-9])(?=[A-Z])", " ", value)
    value = re.sub(r"(?<=[A-Za-z])(?=\d)|(?<=\d)(?=[A-Za-z])", " ", value)
    normalized = re.sub(r"[^a-z0-9]+", " ", value.casefold()).strip()
    number_words = {
        "zero": "0",
        "one": "1",
        "two": "2",
        "three": "3",
        "four": "4",
        "five": "5",
        "six": "6",
        "seven": "7",
        "eight": "8",
        "nine": "9",
    }
    return " ".join(number_words.get(word, word) for word in normalized.split())


def _voice_choice(
    text: str,
    choices: Sequence[str],
    aliases: Sequence[str],
) -> int | None:
    value = _normalize(text)
    for prefix in (
        "i choose ",
        "choose ",
        "i select ",
        "select ",
        "i want ",
        "use ",
        "option ",
    ):
        if value.startswith(prefix):
            value = value[len(prefix):].strip()
            break
    for index, choice in enumerate(choices):
        terms = (choice, aliases[index] if aliases else choice)
        if any(
            value == _normalize(term)
            or value.endswith(f" {_normalize(term)}")
            for term in terms
        ):
            return index
    return None


def prompt_choice(
    title: str,
    message: str,
    choices: Sequence[str],
    *,
    default: int = 0,
    voice_aliases: Sequence[str] = (),
    session_dir: Path | None = None,
) -> int | None:
    """Show a keyboard/voice/StickS3-controllable choice dialog."""
    if not choices:
        raise ValueError("A choice prompt needs at least one option.")
    if not 0 <= default < len(choices):
        raise ValueError("The default choice index is out of range.")
    if voice_aliases and len(voice_aliases) != len(choices):
        raise ValueError("Voice aliases must match the number of choices.")

    import tkinter as tk
    from tkinter import ttk

    root = tk.Tk()
    active_session = session_dir or _session_dir()
    voice_path = (
        active_session / "voice_inbox.jsonl"
        if active_session is not None
        else None
    )
    try:
        voice_offset = voice_path.stat().st_size if voice_path is not None else 0
    except FileNotFoundError:
        voice_offset = 0

    state_path = _activate_prompt("choice", active_session)
    root.title(title)
    root.configure(background="#101827")
    root.resizable(False, False)
    root.attributes("-topmost", True)

    selected = default
    answer: int | None = None
    buttons: list[tk.Button] = []
    voice_after_id: str | None = None
    canvas_ref: tk.Canvas | None = None  # reference for cleanup

    def render_selection() -> None:
        for index, button in enumerate(buttons):
            active = index == selected
            button.configure(
                background="#2563eb" if active else "#1f2937",
                foreground="#ffffff" if active else "#cbd5e1",
                relief="solid" if active else "flat",
                highlightthickness=2 if active else 0,
                highlightbackground="#93c5fd",
            )

    def move_selection(delta: int) -> None:
        nonlocal selected
        selected = (selected + delta) % len(choices)
        render_selection()
        # Scroll to keep selected item visible in the canvas viewport
        if canvas_ref is not None and buttons:
            # Get the selected button's position in the canvas
            button = buttons[selected]
            # Update idle tasks to ensure geometry is calculated
            canvas_ref.update_idletasks()
            # Get button's y position relative to canvas
            y = button.winfo_y()
            button_height = button.winfo_height()
            # Get canvas visible area
            canvas_height = canvas_ref.winfo_height()
            # Calculate scroll fraction to center the button in view
            # Scroll so button is in the middle of visible area
            target_y = y - (canvas_height - button_height) // 2
            # Clamp to valid range
            scroll_region = canvas_ref.bbox("all")
            if scroll_region:
                max_y = scroll_region[3] - canvas_height
                if max_y > 0:
                    fraction = max(0.0, min(1.0, target_y / max_y))
                    canvas_ref.yview_moveto(fraction)

    def cleanup_bindings() -> None:
        """Unbind global mouse wheel events before window destruction."""
        if canvas_ref is not None:
            try:
                canvas_ref.unbind_all("<MouseWheel>")
                canvas_ref.unbind_all("<Button-4>")
                canvas_ref.unbind_all("<Button-5>")
            except Exception:
                pass

    def finish(index: int | None) -> None:
        nonlocal answer, voice_after_id
        answer = index
        # Cancel any pending voice callback
        if voice_after_id is not None:
            try:
                root.after_cancel(voice_after_id)
            except Exception:
                pass
        # Unbind global events BEFORE destroying the window
        cleanup_bindings()
        root.destroy()

    def choose(index: int) -> None:
        nonlocal selected
        selected = index
        finish(index)

    def on_key(event: tk.Event) -> str | None:
        if event.keysym in ("Left", "Up"):
            move_selection(-1)
            return "break"
        if event.keysym in ("Right", "Down"):
            move_selection(1)
            return "break"
        if event.keysym in ("Return", "KP_Enter"):
            finish(selected)
            return "break"
        if event.keysym == "Escape":
            finish(None)
            return "break"
        return None

    def read_voice_choices() -> None:
        nonlocal voice_offset, selected, voice_after_id
        if voice_path is not None:
            try:
                with voice_path.open("rb") as stream:
                    stream.seek(voice_offset)
                    while True:
                        line = stream.readline()
                        if not line:
                            break
                        voice_offset = stream.tell()
                        try:
                            message_data = json.loads(line)
                        except json.JSONDecodeError:
                            continue
                        spoken = message_data.get("text")
                        if not isinstance(spoken, str):
                            continue
                        index = _voice_choice(spoken, choices, voice_aliases)
                        if index is not None:
                            selected = index
                            render_selection()
            except FileNotFoundError:
                pass
        voice_after_id = root.after(100, read_voice_choices)

    try:
        # Main container with scrolling support
        main_frame = tk.Frame(root, background="#101827")
        main_frame.pack(fill="both", expand=True)

        # Canvas with scrollbar for long lists
        canvas = tk.Canvas(main_frame, background="#101827", highlightthickness=0)
        canvas_ref = canvas  # store reference for cleanup
        scrollbar = ttk.Scrollbar(main_frame, orient="vertical", command=canvas.yview)
        canvas.configure(yscrollcommand=scrollbar.set)

        scrollbar.pack(side="right", fill="y")
        canvas.pack(side="left", fill="both", expand=True)

        card = tk.Frame(canvas, background="#101827", padx=28, pady=24)
        canvas_window = canvas.create_window((0, 0), window=card, anchor="nw")

        def on_configure(event: tk.Event) -> None:
            canvas.configure(scrollregion=canvas.bbox("all"))
            canvas.itemconfig(canvas_window, width=event.width)

        card.bind("<Configure>", on_configure)
        canvas.bind("<Configure>", lambda e: canvas.itemconfig(canvas_window, width=e.width))

        # Mouse wheel scrolling
        def on_mousewheel(event: tk.Event) -> None:
            canvas.yview_scroll(int(-1 * (event.delta / 120)), "units")

        canvas.bind_all("<MouseWheel>", on_mousewheel)
        canvas.bind_all("<Button-4>", lambda e: canvas.yview_scroll(-1, "units"))
        canvas.bind_all("<Button-5>", lambda e: canvas.yview_scroll(1, "units"))

        tk.Label(
            card,
            text=title,
            font=("Segoe UI", 18, "bold"),
            foreground="#f8fafc",
            background="#101827",
        ).pack(anchor="w")
        tk.Label(
            card,
            text=message,
            font=("Segoe UI", 10),
            foreground="#94a3b8",
            background="#101827",
            wraplength=500,
            justify="left",
        ).pack(anchor="w", pady=(8, 18))

        for index, choice in enumerate(choices):
            button = tk.Button(
                card,
                text=choice,
                font=("Segoe UI", 11, "bold"),
                anchor="w",
                padx=16,
                pady=12,
                borderwidth=0,
                cursor="hand2",
                command=lambda selected_index=index: choose(selected_index),
            )
            button.pack(fill="x", pady=4)
            buttons.append(button)

        tk.Label(
            card,
            text="Left/Right or voice selects | Front/Enter accepts | Back/Esc cancels",
            font=("Segoe UI", 9),
            foreground="#94a3b8",
            background="#101827",
        ).pack(anchor="w", pady=(16, 0))

        render_selection()
        root.bind("<KeyPress>", on_key)
        root.protocol("WM_DELETE_WINDOW", lambda: finish(None))
        root.update_idletasks()

        # Limit max height to 80% of screen height
        max_height = int(root.winfo_screenheight() * 0.8)
        req_height = min(root.winfo_reqheight(), max_height)
        width = root.winfo_reqwidth()
        x = (root.winfo_screenwidth() - width) // 2
        y = (root.winfo_screenheight() - req_height) // 2
        root.geometry(f"{width}x{req_height}+{x}+{y}")

        voice_after_id = root.after(100, read_voice_choices)
        root.after(100, root.focus_force)
        root.mainloop()
        return answer
    finally:
        # Ensure voice callback is cancelled on any exit path
        if voice_after_id is not None:
            try:
                root.after_cancel(voice_after_id)
            except Exception:
                pass
        _deactivate_prompt(state_path)


def prompt_text(
    title: str,
    message: str,
    *,
    initial: str = "",
    password: bool = False,
) -> str | None:
    """Show a keyboard-only field; password input is masked and never spoken."""
    import tkinter as tk

    root = tk.Tk()
    state_path = _activate_prompt("text")
    root.title(title)
    root.configure(background="#101827")
    root.resizable(False, False)
    root.attributes("-topmost", True)
    answer: str | None = None

    def finish(value: str | None) -> None:
        nonlocal answer
        answer = value
        root.destroy()

    try:
        card = tk.Frame(root, background="#101827", padx=28, pady=24)
        card.pack(fill="both", expand=True)
        tk.Label(
            card,
            text=title,
            font=("Segoe UI", 18, "bold"),
            foreground="#f8fafc",
            background="#101827",
        ).pack(anchor="w")
        tk.Label(
            card,
            text=message,
            font=("Segoe UI", 10),
            foreground="#94a3b8",
            background="#101827",
            wraplength=500,
            justify="left",
        ).pack(anchor="w", pady=(8, 16))
        value = tk.StringVar(value=initial)
        entry = tk.Entry(
            card,
            textvariable=value,
            show="*" if password else "",
            font=("Segoe UI", 12),
            width=48,
        )
        entry.pack(fill="x", pady=(0, 18))
        buttons = tk.Frame(card, background="#101827")
        buttons.pack(fill="x")
        tk.Button(
            buttons,
            text="Cancel",
            command=lambda: finish(None),
            padx=14,
            pady=8,
        ).pack(side="right")
        tk.Button(
            buttons,
            text="Continue",
            command=lambda: finish(value.get()),
            padx=14,
            pady=8,
        ).pack(side="right", padx=(0, 8))
        entry.bind("<Return>", lambda _event: finish(value.get()))
        root.bind("<Escape>", lambda _event: finish(None))
        root.protocol("WM_DELETE_WINDOW", lambda: finish(None))
        root.update_idletasks()
        width = root.winfo_reqwidth()
        height = root.winfo_reqheight()
        x = (root.winfo_screenwidth() - width) // 2
        y = (root.winfo_screenheight() - height) // 2
        root.geometry(f"{width}x{height}+{x}+{y}")
        root.after(100, entry.focus_force)
        root.mainloop()
        return answer
    finally:
        _deactivate_prompt(state_path)
