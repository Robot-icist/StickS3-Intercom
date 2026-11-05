import json
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from voice_actions import (
    VoiceCommandRouter,
    load_actions,
    parse_intent,
)


class ParseIntentTests(unittest.TestCase):
    def test_unprefixed_transcript_stays_dictation(self) -> None:
        self.assertEqual(
            parse_intent("Please explain this error").target, "dictation"
        )

    def test_explicit_targets_are_recognized(self) -> None:
        cases = {
            "OpenCode: fix the failing test": ("opencode", "fix the failing test"),
            "Open code, explain the diff": ("opencode", "explain the diff"),
            "Claude Code: summarize this file": ("claude", "summarize this file"),
            "GitHub Copilot: explain this change": (
                "copilot",
                "explain this change",
            ),
            "Copilot: inspect the error": ("copilot", "inspect the error"),
            "Run action: show action setup": ("action", "show action setup"),
            "Dictate: do not send this to an agent": (
                "dictation",
                "do not send this to an agent",
            ),
        }
        for transcript, expected in cases.items():
            with self.subTest(transcript=transcript):
                intent = parse_intent(transcript)
                self.assertEqual((intent.target, intent.content), expected)

    def test_partial_prefix_is_not_dictation(self) -> None:
        self.assertEqual(parse_intent("Claude").target, "pending")


class VoiceCommandRouterTests(unittest.TestCase):
    def make_router(self, config: Path) -> VoiceCommandRouter:
        return VoiceCommandRouter(
            opencode_command="opencode",
            opencode_session=None,
            claude_command="claude",
            claude_session=None,
            claude_cwd=None,
            copilot_command="copilot",
            copilot_cwd=None,
            actions_path=config,
        )

    def test_actions_require_confirmation_and_use_fixed_arguments(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            config = Path(temporary) / "actions.json"
            config.write_text(
                json.dumps({
                    "actions": {
                        "show setup": {
                            "command": ["python", "-c", "print('safe')"],
                            "description": "A test action",
                        }
                    }
                }),
                encoding="utf-8",
            )
            router = self.make_router(config)
            with (
                patch("builtins.input", return_value="yes"),
                patch("voice_actions.shutil.which", return_value="python.exe"),
                patch(
                    "voice_actions.subprocess.run",
                    return_value=subprocess.CompletedProcess(
                        [], 0, stdout="", stderr=""
                    ),
                ) as run,
            ):
                result = router.handle("run action: show setup")

            self.assertIsNone(result)
            run.assert_called_once()
            self.assertEqual(
                run.call_args.args[0],
                ("python.exe", "-c", "print('safe')"),
            )

    def test_unconfigured_action_name_is_not_executed(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            config = Path(temporary) / "actions.json"
            config.write_text(
                json.dumps({
                    "actions": {
                        "show setup": {"command": ["echo", "ok"]}
                    }
                }),
                encoding="utf-8",
            )
            router = self.make_router(config)
            with (
                patch("builtins.input", return_value="yes"),
                patch("voice_actions.subprocess.run") as run,
            ):
                router.handle(
                    "run action: show setup; delete everything"
                )
            run.assert_not_called()

    def test_declined_action_does_not_run(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            config = Path(temporary) / "actions.json"
            config.write_text(
                json.dumps({"actions": {"test": {"command": ["echo", "ok"]}}}),
                encoding="utf-8",
            )
            router = self.make_router(config)
            with (
                patch("builtins.input", return_value="no"),
                patch("voice_actions.subprocess.run") as run,
            ):
                router.handle("run action: test")
            run.assert_not_called()

    def test_opencode_uses_selected_session_and_cli_prompt(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            router = self.make_router(Path(temporary) / "missing.json")
            with (
                patch("builtins.input", side_effect=("1", "yes")),
                patch(
                    "voice_actions.shutil.which",
                    return_value="opencode.cmd",
                ),
                patch(
                    "voice_actions.subprocess.run",
                    side_effect=(
                        subprocess.CompletedProcess(
                            [], 0, stdout=json.dumps([
                                {
                                    "id": "session-1",
                                    "title": "Project",
                                    "directory": temporary,
                                }
                            ]),
                            stderr="",
                        ),
                        subprocess.CompletedProcess([], 0),
                    ),
                ) as run,
            ):
                router.handle("OpenCode: summarize this project")

            self.assertEqual(router.opencode_session, "session-1")
            self.assertEqual(run.call_args_list[0].args[0], [
                "opencode.cmd",
                "session",
                "list",
                "--format",
                "json",
            ])
            self.assertEqual(run.call_args_list[1].args[0], [
                "opencode.cmd",
                "run",
                "--session",
                "session-1",
                "summarize this project",
            ])
            self.assertNotIn(
                "--auto", run.call_args_list[1].args[0]
            )
            self.assertEqual(run.call_args_list[1].kwargs["cwd"], Path(temporary))

    def test_claude_invocation_uses_argv_and_optional_resume(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            router = self.make_router(Path(temporary) / "missing.json")
            router.claude_session = "claude-session-1"
            with (
                patch("builtins.input", return_value="yes"),
                patch("voice_actions.shutil.which", return_value="claude.exe"),
                patch(
                    "voice_actions.subprocess.run",
                    return_value=subprocess.CompletedProcess([], 0),
                ) as run,
            ):
                router.handle("Claude: inspect the latest change")

            self.assertEqual(
                run.call_args.args[0],
                [
                    "claude.exe",
                    "-p",
                    "--output-format",
                    "text",
                    "--resume",
                    "claude-session-1",
                    "inspect the latest change",
                ],
            )
            self.assertFalse(run.call_args.kwargs.get("shell", False))

    def test_copilot_uses_programmatic_prompt_without_auto_approval(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            router = self.make_router(Path(temporary) / "missing.json")
            with (
                patch("builtins.input", return_value="yes"),
                patch("voice_actions.shutil.which", return_value="copilot.exe"),
                patch(
                    "voice_actions.subprocess.run",
                    return_value=subprocess.CompletedProcess([], 0),
                ) as run,
            ):
                router.handle("Copilot: explain the latest change")

            self.assertEqual(
                run.call_args.args[0],
                ["copilot.exe", "--prompt", "explain the latest change"],
            )
            self.assertNotIn("--allow-all-tools", run.call_args.args[0])
            self.assertFalse(run.call_args.kwargs.get("shell", False))

    def test_invalid_action_config_fails_closed(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            config = Path(temporary) / "actions.json"
            config.write_text(
                '{"actions":{"bad":{"command":"python script.py"}}}',
                encoding="utf-8",
            )
            with self.assertRaisesRegex(ValueError, "string-array"):
                load_actions(config)


if __name__ == "__main__":
    unittest.main()
