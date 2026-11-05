import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from desktop_app import (
    _receiver_command,
    load_action_definitions,
    save_action_definitions,
)


class DesktopActionConfigTests(unittest.TestCase):
    def test_saves_and_reloads_an_action_for_the_existing_router(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "actions.json"
            definitions = {
                "OpenCode review": {
                    "command": ["opencode", "run", "--prompt"],
                    "cwd": temporary,
                    "description": "Review the current change",
                    "interactive": True,
                }
            }

            save_action_definitions(path, definitions)

            self.assertEqual(load_action_definitions(path), definitions)

    def test_invalid_action_does_not_replace_existing_config(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "actions.json"
            original = '{"actions": {}}\n'
            path.write_text(original, encoding="utf-8")

            with self.assertRaises(ValueError):
                save_action_definitions(
                    path,
                    {"broken": {"command": []}},
                )

            self.assertEqual(path.read_text(encoding="utf-8"), original)
            self.assertFalse(path.with_suffix(".json.tmp").exists())

    def test_receiver_default_enables_live_voice_command_routing(self) -> None:
        output = Path("recordings")
        command = _receiver_command(output)

        self.assertIn("--transcribe", command)
        self.assertIn("--voice-commands", command)
        self.assertIn("--transcribe-arg=--partial-live", command)
        self.assertIn("--output", command)
        self.assertEqual(command[command.index("--output") + 1], str(output))

    def test_frozen_receiver_starts_as_a_separate_companion_process(self) -> None:
        with (
            patch("desktop_app.sys.frozen", True, create=True),
            patch("desktop_app.sys.executable", "StickS3Companion.exe"),
        ):
            command = _receiver_command(Path("recordings"))

        self.assertEqual(command[:2], ["StickS3Companion.exe", "--internal-receiver"])


if __name__ == "__main__":
    unittest.main()
