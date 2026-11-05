"""A small desktop dashboard for the StickS3 BLE audio companion."""

from __future__ import annotations

import json
import os
import queue
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path
from tkinter import (
    BooleanVar,
    Listbox,
    StringVar,
    Text,
    Tk,
    Toplevel,
    messagebox,
    ttk,
)

from voice_actions import default_actions_path, load_actions


APP_NAME = "StickS3 Companion"
_COLORS = {
    "background": "#0b1220",
    "surface": "#111c2e",
    "surface_alt": "#17243a",
    "border": "#263751",
    "text": "#e6edf7",
    "muted": "#9aabc2",
    "accent": "#55c2a3",
    "accent_dark": "#173d3a",
    "danger": "#ff7d88",
}


def recordings_path() -> Path:
    return default_actions_path().parent / "recordings"


def opencode_working_directory() -> Path:
    configured = os.environ.get("OPENCODE_WORKING_DIRECTORY")
    if configured:
        return Path(configured).expanduser()

    starts = (
        Path.cwd(),
        Path(__file__).resolve().parent,
        Path(sys.executable).resolve().parent,
    )
    for start in starts:
        for candidate in (start, *start.parents):
            if (candidate / "platformio.ini").is_file():
                return candidate
    return Path.home()


def load_action_definitions(path: Path) -> dict[str, dict[str, object]]:
    try:
        configuration = json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError:
        return {}
    except (OSError, json.JSONDecodeError) as error:
        raise ValueError(f"Cannot read actions from {path}: {error}") from error

    definitions = (
        configuration.get("actions")
        if isinstance(configuration, dict)
        else None
    )
    if not isinstance(definitions, dict):
        raise ValueError(f"{path} must contain an 'actions' object.")
    load_actions(path)
    return definitions


def save_action_definitions(
    path: Path, definitions: dict[str, dict[str, object]]
) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    payload = json.dumps({"actions": definitions}, indent=2, ensure_ascii=False)
    temporary_path = path.with_suffix(path.suffix + ".tmp")
    try:
        temporary_path.write_text(payload + "\n", encoding="utf-8")
        load_actions(temporary_path)
        temporary_path.replace(path)
    except (OSError, ValueError) as error:
        try:
            temporary_path.unlink(missing_ok=True)
        except OSError:
            pass
        raise ValueError(f"Cannot save actions to {path}: {error}") from error


def _receiver_command(
    output_dir: Path, opencode_cwd: Path | None = None
) -> list[str]:
    arguments = [
        "--transcribe",
        "--voice-commands",
        "--transcribe-arg=--partial-live",
        "--output",
        str(output_dir),
    ]
    if opencode_cwd is not None:
        arguments.extend(("--opencode-cwd", str(opencode_cwd)))
    if getattr(sys, "frozen", False):
        return [sys.executable, "--internal-receiver", *arguments]
    receiver = Path(__file__).resolve().with_name("launcher.py")
    return [sys.executable, "-u", str(receiver), "--internal-receiver", *arguments]


class ActionEditor:
    def __init__(self, parent: Tk, config_path: Path) -> None:
        self.window = Toplevel(parent)
        self.window.title("Configure voice actions")
        self.window.configure(background=_COLORS["background"])
        self.window.geometry("860x590")
        self.window.minsize(760, 500)
        self.config_path = config_path
        self.actions = load_action_definitions(config_path)
        self.current_name: str | None = None

        self.name = StringVar()
        self.executable = StringVar()
        self.cwd = StringVar()
        self.description = StringVar()
        self.aliases = StringVar()
        self.interactive = BooleanVar(value=True)

        self._build()
        self._refresh_list()

    def _build(self) -> None:
        outer = ttk.Frame(self.window, padding=22, style="App.TFrame")
        outer.pack(fill="both", expand=True)
        ttk.Label(
            outer, text="Voice actions", style="Title.TLabel"
        ).pack(anchor="w")
        ttk.Label(
            outer,
            text=(
                "Say “run action: name” or ask naturally in English or French. "
                "Add translated names as comma-separated aliases. "
                "Actions ask for confirmation by default."
            ),
            style="Muted.TLabel",
        ).pack(anchor="w", pady=(4, 18))

        content = ttk.Frame(outer, style="App.TFrame")
        content.pack(fill="both", expand=True)
        content.columnconfigure(1, weight=1)
        content.rowconfigure(0, weight=1)

        left = ttk.Frame(content, style="App.TFrame")
        left.grid(row=0, column=0, sticky="nsw", padx=(0, 20))
        ttk.Label(left, text="Configured actions", style="Section.TLabel").pack(
            anchor="w", pady=(0, 8)
        )
        self.action_list = Listbox(
            left,
            width=25,
            height=16,
            background=_COLORS["surface"],
            foreground=_COLORS["text"],
            selectbackground=_COLORS["accent_dark"],
            selectforeground=_COLORS["text"],
            borderwidth=0,
            highlightthickness=1,
            highlightbackground=_COLORS["border"],
            activestyle="none",
            font=("Segoe UI", 10),
        )
        self.action_list.pack(fill="y", expand=True)
        self.action_list.bind("<<ListboxSelect>>", self._select_action)

        list_buttons = ttk.Frame(left, style="App.TFrame")
        list_buttons.pack(fill="x", pady=(10, 0))
        ttk.Button(list_buttons, text="New", command=self._new_action).pack(
            side="left"
        )
        ttk.Button(
            list_buttons, text="Delete", command=self._delete_action
        ).pack(side="right")

        form = ttk.Frame(content, style="Card.TFrame", padding=18)
        form.grid(row=0, column=1, sticky="nsew")
        form.columnconfigure(0, weight=1)
        ttk.Label(form, text="Action details", style="Section.TLabel").grid(
            row=0, column=0, sticky="w", pady=(0, 12)
        )
        self._entry(form, 1, "Action name", self.name)
        self._entry(form, 3, "Program or executable", self.executable)

        ttk.Label(form, text="Arguments (one per line)", style="Field.TLabel").grid(
            row=5, column=0, sticky="w", pady=(12, 4)
        )
        self.arguments = Text(
            form,
            height=5,
            wrap="word",
            background=_COLORS["surface"],
            foreground=_COLORS["text"],
            insertbackground=_COLORS["text"],
            borderwidth=0,
            padx=10,
            pady=8,
            font=("Consolas", 10),
        )
        self.arguments.grid(row=6, column=0, sticky="ew")
        self._entry(form, 7, "Working directory (optional)", self.cwd)
        self._entry(form, 9, "Description", self.description)
        self._entry(form, 11, "Voice aliases (comma-separated)", self.aliases)
        ttk.Checkbutton(
            form,
            text="Ask before running this action",
            variable=self.interactive,
        ).grid(row=13, column=0, sticky="w", pady=(14, 8))
        ttk.Label(
            form,
            text="Commands run as fixed arguments, never through a shell.",
            style="Muted.TLabel",
        ).grid(row=14, column=0, sticky="w")
        ttk.Button(
            form, text="Save action", style="Accent.TButton", command=self._save
        ).grid(row=15, column=0, sticky="e", pady=(20, 0))

    @staticmethod
    def _entry(
        parent: ttk.Frame, row: int, label: str, variable: StringVar
    ) -> None:
        ttk.Label(parent, text=label, style="Field.TLabel").grid(
            row=row, column=0, sticky="w", pady=(8, 4)
        )
        ttk.Entry(parent, textvariable=variable).grid(
            row=row + 1, column=0, sticky="ew"
        )

    def _refresh_list(self, selected: str | None = None) -> None:
        self.action_list.delete(0, "end")
        names = sorted(self.actions, key=str.casefold)
        for name in names:
            self.action_list.insert("end", name)
        if selected in names:
            index = names.index(selected)
            self.action_list.selection_set(index)
            self.action_list.see(index)

    def _select_action(self, event: object = None) -> None:
        if (
            event is not None
            and getattr(event, "widget", self.action_list) is not self.action_list
        ):
            return
        selection = self.action_list.curselection()
        if not selection:
            return
        name = self.action_list.get(selection[0])
        definition = self.actions[name]
        command = definition.get("command", [])
        if not isinstance(command, list):
            return
        self.current_name = name
        self.name.set(name)
        self.executable.set(command[0] if command else "")
        self.arguments.delete("1.0", "end")
        self.arguments.insert("1.0", "\n".join(command[1:]))
        cwd = definition.get("cwd", "")
        self.cwd.set(str(cwd) if cwd is not None else "")
        self.description.set(str(definition.get("description", "")))
        aliases = definition.get("aliases", [])
        self.aliases.set(
            ", ".join(alias for alias in aliases if isinstance(alias, str))
            if isinstance(aliases, list) else ""
        )
        self.interactive.set(bool(definition.get("interactive", True)))

    def _new_action(self) -> None:
        self.current_name = None
        self.action_list.selection_clear(0, "end")
        self.name.set("")
        self.executable.set("")
        self.arguments.delete("1.0", "end")
        self.cwd.set("")
        self.description.set("")
        self.aliases.set("")
        self.interactive.set(True)

    def _delete_action(self) -> None:
        if self.current_name is None:
            return
        if not messagebox.askyesno(
            "Delete action",
            f"Remove “{self.current_name}” from the action list?",
            parent=self.window,
        ):
            return
        updated_actions = self.actions.copy()
        del updated_actions[self.current_name]
        try:
            save_action_definitions(self.config_path, updated_actions)
        except ValueError as error:
            messagebox.showerror(
                "Could not save actions", str(error), parent=self.window
            )
            return
        self.actions = updated_actions
        self._new_action()
        self._refresh_list()

    def _save(self) -> None:
        name = self.name.get().strip()
        executable = self.executable.get().strip()
        if not name or not executable:
            messagebox.showwarning(
                "Action needs a name and program",
                "Enter both an action name and the program to run.",
                parent=self.window,
            )
            return
        existing_name = next(
            (
                action_name for action_name in self.actions
                if action_name.casefold() == name.casefold()
            ),
            None,
        )
        if (
            existing_name is not None
            and existing_name != self.current_name
        ):
            messagebox.showwarning(
                "Action already exists",
                f"An action named “{existing_name}” already exists.",
                parent=self.window,
            )
            return

        arguments = [
            line for line in self.arguments.get("1.0", "end-1c").splitlines()
            if line
        ]
        definition: dict[str, object] = {
            "command": [executable, *arguments],
            "description": self.description.get().strip(),
            "interactive": self.interactive.get(),
        }
        aliases = [
            alias.strip() for alias in self.aliases.get().split(",")
            if alias.strip()
        ]
        if aliases:
            definition["aliases"] = aliases
        cwd = self.cwd.get().strip()
        if cwd:
            definition["cwd"] = cwd

        updated_actions = self.actions.copy()
        if self.current_name is not None and self.current_name != name:
            del updated_actions[self.current_name]
        updated_actions[name] = definition
        try:
            save_action_definitions(self.config_path, updated_actions)
        except ValueError as error:
            messagebox.showerror(
                "Could not save actions", str(error), parent=self.window
            )
            return
        self.actions = updated_actions
        self.current_name = name
        self._refresh_list(name)
        messagebox.showinfo(
            "Action saved",
            f"Say “run action: {name}” to use this action.",
            parent=self.window,
        )


class CompanionApp:
    def __init__(self, root: Tk) -> None:
        self.root = root
        self.root.title(APP_NAME)
        self.root.geometry("840x650")
        self.root.minsize(700, 540)
        self.root.configure(background=_COLORS["background"])
        self.config_path = default_actions_path()
        self.output_dir = recordings_path()
        self._log_directory = tempfile.TemporaryDirectory(
            prefix="sticks3-companion-"
        )
        self.log_path = Path(self._log_directory.name) / "companion.log"
        self.messages: queue.Queue[tuple[str, object]] = queue.Queue()
        self.process: subprocess.Popen[bytes] | None = None
        self.reader: threading.Thread | None = None
        self.closing = False
        self.status = StringVar(value="Starting")
        self.status_detail = StringVar(
            value="Preparing microphone and speech recognition"
        )

        self._style()
        self._build()
        self.root.protocol("WM_DELETE_WINDOW", self.close)
        self.root.after(200, self.start_receiver)
        self.root.after(100, self._poll_messages)

    def _style(self) -> None:
        style = ttk.Style(self.root)
        style.theme_use("clam")
        style.configure(
            ".",
            background=_COLORS["background"],
            foreground=_COLORS["text"],
            font=("Segoe UI", 10),
        )
        style.configure("App.TFrame", background=_COLORS["background"])
        style.configure(
            "Card.TFrame",
            background=_COLORS["surface"],
            borderwidth=1,
            relief="solid",
        )
        style.configure(
            "Title.TLabel",
            background=_COLORS["background"],
            foreground=_COLORS["text"],
            font=("Segoe UI Semibold", 21),
        )
        style.configure(
            "Section.TLabel",
            background=_COLORS["surface"],
            foreground=_COLORS["text"],
            font=("Segoe UI Semibold", 12),
        )
        style.configure(
            "Muted.TLabel",
            background=_COLORS["background"],
            foreground=_COLORS["muted"],
        )
        style.configure(
            "Field.TLabel",
            background=_COLORS["surface"],
            foreground=_COLORS["muted"],
        )
        style.configure(
            "Status.TLabel",
            background=_COLORS["surface_alt"],
            foreground=_COLORS["accent"],
            padding=(10, 5),
            font=("Segoe UI Semibold", 10),
        )
        style.configure(
            "TEntry",
            fieldbackground=_COLORS["surface_alt"],
            foreground=_COLORS["text"],
            insertcolor=_COLORS["text"],
            padding=8,
        )
        style.configure(
            "TCheckbutton",
            background=_COLORS["surface"],
            foreground=_COLORS["text"],
        )
        style.map(
            "TCheckbutton",
            background=[("active", _COLORS["surface"])],
        )
        style.configure(
            "TButton",
            background=_COLORS["surface_alt"],
            foreground=_COLORS["text"],
            padding=(12, 8),
            borderwidth=0,
        )
        style.map(
            "TButton",
            background=[("active", _COLORS["border"])],
        )
        style.configure(
            "Accent.TButton",
            background=_COLORS["accent_dark"],
            foreground=_COLORS["accent"],
        )
        style.map(
            "Accent.TButton",
            background=[("active", "#20564d")],
        )

    def _build(self) -> None:
        outer = ttk.Frame(self.root, style="App.TFrame", padding=24)
        outer.pack(fill="both", expand=True)

        header = ttk.Frame(outer, style="App.TFrame")
        header.pack(fill="x")
        title_block = ttk.Frame(header, style="App.TFrame")
        title_block.pack(side="left", fill="x", expand=True)
        ttk.Label(title_block, text=APP_NAME, style="Title.TLabel").pack(anchor="w")
        ttk.Label(
            title_block,
            text="BLE microphone · live dictation · voice actions",
            style="Muted.TLabel",
        ).pack(anchor="w", pady=(4, 0))
        ttk.Label(header, textvariable=self.status, style="Status.TLabel").pack(
            side="right", anchor="n"
        )

        status_card = ttk.Frame(outer, style="Card.TFrame", padding=18)
        status_card.pack(fill="x", pady=(20, 14))
        ttk.Label(
            status_card, text="Microphone connection", style="Section.TLabel"
        ).pack(anchor="w")
        ttk.Label(
            status_card,
            textvariable=self.status_detail,
            style="Field.TLabel",
        ).pack(anchor="w", pady=(6, 14))
        controls = ttk.Frame(status_card, style="Card.TFrame")
        controls.pack(fill="x")
        self.start_button = ttk.Button(
            controls,
            text="Start listening",
            style="Accent.TButton",
            command=self.start_receiver,
        )
        self.start_button.pack(side="left")
        self.stop_button = ttk.Button(
            controls, text="Stop", command=self.stop_receiver, state="disabled"
        )
        self.stop_button.pack(side="left", padx=(8, 0))
        ttk.Button(
            controls, text="Configure actions", command=self.open_actions
        ).pack(side="right")

        features = ttk.Frame(outer, style="App.TFrame")
        features.pack(fill="x", pady=(0, 14))
        ttk.Label(
            features,
            text="Live transcription and voice actions start automatically. "
            "Unprefixed speech is dictated; say “OpenCode …” or "
            "“run action: …” to route a command.",
            style="Muted.TLabel",
            wraplength=760,
        ).pack(anchor="w")

        log_card = ttk.Frame(outer, style="Card.TFrame", padding=14)
        log_card.pack(fill="both", expand=True)
        log_header = ttk.Frame(log_card, style="Card.TFrame")
        log_header.pack(fill="x", pady=(0, 8))
        ttk.Label(log_header, text="Activity", style="Section.TLabel").pack(
            side="left"
        )
        ttk.Button(
            log_header, text="Open recordings", command=self.open_recordings
        ).pack(side="right")
        self.log = Text(
            log_card,
            wrap="word",
            background=_COLORS["background"],
            foreground=_COLORS["text"],
            insertbackground=_COLORS["text"],
            selectbackground=_COLORS["accent_dark"],
            borderwidth=0,
            padx=12,
            pady=10,
            font=("Consolas", 9),
            state="disabled",
        )
        self.log.pack(fill="both", expand=True)
        self.log.tag_configure("error", foreground=_COLORS["danger"])
        self.log.tag_configure("transcript", foreground="#b7ead9")
        self._append_log("Starting BLE microphone and live transcription...")

    def _append_log(self, line: str) -> None:
        self.log.configure(state="normal")
        tag = (
            "error"
            if any(word in line.casefold() for word in ("error", "failed"))
            else ""
        )
        if line.startswith("  "):
            self.log.insert("end", line + "\n", "transcript")
        elif tag:
            self.log.insert("end", line + "\n", tag)
        else:
            self.log.insert("end", line + "\n")
        line_count = int(self.log.index("end-1c").split(".")[0])
        if line_count > 500:
            self.log.delete("1.0", f"{line_count - 500}.0")
        self.log.see("end")
        self.log.configure(state="disabled")

    def start_receiver(self) -> None:
        if self.process is not None and self.process.poll() is None:
            return
        command = _receiver_command(
            self.output_dir, opencode_working_directory()
        )
        environment = os.environ.copy()
        environment["PYTHONUNBUFFERED"] = "1"
        environment["STICKS3_INTERACTION_DIR"] = str(
            self.config_path.parent / "interaction"
        )
        environment["STICKS3_COMPANION_LOG"] = str(self.log_path)
        try:
            self.log_path.parent.mkdir(parents=True, exist_ok=True)
            self.log_path.write_text("", encoding="utf-8")
        except OSError as error:
            self.status.set("Could not start")
            self.status_detail.set("The activity log could not be created")
            self._append_log(f"ERROR: could not create activity log: {error}")
            return
        try:
            self.process = subprocess.Popen(
                command,
                cwd=str(Path.home()),
                env=environment,
                stdin=subprocess.DEVNULL,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
            )
        except OSError as error:
            self.status.set("Could not start")
            self.status_detail.set(str(error))
            self._append_log(f"ERROR: could not start receiver: {error}")
            return

        self.status.set("Starting")
        self.status_detail.set("Loading speech model and connecting to StickS3")
        self.start_button.configure(state="disabled")
        self.stop_button.configure(state="normal")
        self._append_log(
            "Receiver started. Loading speech model and scanning for StickS3..."
        )
        self.reader = threading.Thread(
            target=self._read_process_output,
            args=(self.process, self.log_path),
            name="companion-output",
            daemon=True,
        )
        self.reader.start()

    def _read_process_output(
        self, process: subprocess.Popen[bytes], log_path: Path
    ) -> None:
        offset = 0
        idle_after_exit = 0
        while idle_after_exit < 20:
            lines: list[str] = []
            try:
                with log_path.open("r", encoding="utf-8", errors="replace") as stream:
                    stream.seek(offset)
                    lines = stream.readlines()
                    offset = stream.tell()
            except OSError as error:
                lines = [f"ERROR: could not read activity log: {error}"]
            for line in lines:
                self.messages.put(("output", line.rstrip("\r\n")))
            if process.poll() is not None and not lines:
                idle_after_exit += 1
            else:
                idle_after_exit = 0
            time.sleep(0.1)
        self.messages.put(("exit", process.wait()))

    def _poll_messages(self) -> None:
        try:
            while True:
                kind, value = self.messages.get_nowait()
                if kind == "output":
                    line = str(value)
                    self._append_log(line)
                    folded = line.casefold()
                    if "companion connected and microphone ready" in folded:
                        self.status.set("Listening")
                        self.status_detail.set(
                            "Connected. Use the StickS3 joystick button to speak."
                        )
                    elif "connecting to" in folded or "scanning" in folded:
                        self.status.set("Connecting")
                        self.status_detail.set("Searching for the paired StickS3")
                    elif "loading" in folded and "model" in folded:
                        self.status.set("Loading speech model")
                        self.status_detail.set(
                            "Preparing live transcription (first run downloads the model)"
                        )
                    elif "ready. focus" in folded:
                        self.status_detail.set(
                            "Transcription ready. Focus a text field before dictating."
                        )
                elif kind == "exit":
                    return_code = int(value)
                    self.process = None
                    self.start_button.configure(state="normal")
                    self.stop_button.configure(state="disabled")
                    if self.closing:
                        return
                    self.status.set("Disconnected" if return_code else "Stopped")
                    self.status_detail.set(
                        "Receiver stopped. Start listening to reconnect."
                    )
                    self._append_log(
                        "Receiver stopped." if return_code == 0
                        else f"Receiver exited with code {return_code}."
                    )
        except queue.Empty:
            pass
        if not self.closing:
            self.root.after(100, self._poll_messages)

    def stop_receiver(self) -> None:
        process = self.process
        if process is None or process.poll() is not None:
            self.status.set("Stopped")
            self.start_button.configure(state="normal")
            self.stop_button.configure(state="disabled")
            return
        self.status.set("Stopping")
        self.status_detail.set("Closing the microphone connection")
        process.terminate()

    def open_actions(self) -> None:
        try:
            ActionEditor(self.root, self.config_path)
        except ValueError as error:
            messagebox.showerror("Invalid actions configuration", str(error), parent=self.root)

    def open_recordings(self) -> None:
        self.output_dir.mkdir(parents=True, exist_ok=True)
        try:
            os.startfile(str(self.output_dir))
        except (AttributeError, OSError) as error:
            messagebox.showerror(
                "Could not open recordings folder", str(error), parent=self.root
            )

    def close(self) -> None:
        self.closing = True
        process = self.process
        if process is not None and process.poll() is None:
            process.terminate()
            log_directory = self._log_directory

            def cleanup_worker() -> None:
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
                time.sleep(2)
                log_directory.cleanup()

            threading.Thread(
                target=cleanup_worker,
                name="companion-cleanup",
                daemon=False,
            ).start()
        else:
            self._log_directory.cleanup()
        self.root.destroy()


def main() -> None:
    root = Tk()
    CompanionApp(root)
    root.mainloop()


if __name__ == "__main__":
    main()
