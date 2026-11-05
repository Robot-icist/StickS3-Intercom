import json
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest.mock import Mock, patch

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
            "  RUN   ACTION : show setup": ("action", "show setup"),
            "run-action: show setup": ("action", "show setup"),
            "rUnAcTiOn: show setup": ("action", "show setup"),
            "opencode: inspect this project": ("opencode", "inspect this project"),
            "open-code: inspect this project": ("opencode", "inspect this project"),
            "open_code inspect this project": (
                "opencode",
                "inspect this project",
            ),
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

    def test_action_configuration_is_reloaded_after_startup(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            config = Path(temporary) / "actions.json"
            router = self.make_router(config)
            config.write_text(
                json.dumps({
                    "actions": {
                        "fresh action": {
                            "command": ["python", "-c", "pass"],
                            "description": "Added while the companion was running",
                        }
                    }
                }),
                encoding="utf-8",
            )

            with patch.object(router, "_run_action") as run_action:
                router.route("run action: fresh action")

            run_action.assert_called_once_with("fresh action")

    def test_action_name_matching_ignores_case_spacing_and_punctuation(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            config = Path(temporary) / "actions.json"
            config.write_text(
                json.dumps({
                    "actions": {
                        "Git Status": {"command": ["git", "status"]}
                    }
                }),
                encoding="utf-8",
            )
            router = self.make_router(config)
            with patch.object(router, "_run_action") as run_action:
                router.route("RUN ACTION: Git-Status")

            run_action.assert_called_once_with("git status")

    def test_english_context_and_asr_typo_match_an_action(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            config = Path(temporary) / "actions.json"
            config.write_text(
                json.dumps({
                    "actions": {
                        "git status": {"command": ["git", "status"]}
                    }
                }),
                encoding="utf-8",
            )
            router = self.make_router(config)
            with patch.object(router, "_run_action") as run_action:
                router.route("Could you please run the GitStatus action now?")

            run_action.assert_called_once_with("git status")

    def test_french_context_and_translated_alias_match_an_action(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            config = Path(temporary) / "actions.json"
            config.write_text(
                json.dumps({
                    "actions": {
                        "git status": {
                            "command": ["git", "status"],
                            "aliases": ["état du dépôt"],
                        }
                    }
                }),
                encoding="utf-8",
            )
            router = self.make_router(config)
            with patch.object(router, "_run_action") as run_action:
                router.route(
                    "Peux-tu lancer l’action « état du dépôt » maintenant ?"
                )

            run_action.assert_called_once_with("git status")

    def test_unmatched_contextual_speech_remains_dictation(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            config = Path(temporary) / "actions.json"
            config.write_text(
                json.dumps({
                    "actions": {
                        "git status": {"command": ["git", "status"]}
                    }
                }),
                encoding="utf-8",
            )
            router = self.make_router(config)

            self.assertEqual(
                router.route("Please run the unrelated task"),
                "Please run the unrelated task",
            )

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

    def test_interactive_action_starts_asynchronously_with_voice_session(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            config = Path(temporary) / "actions.json"
            config.write_text(
                json.dumps({
                    "actions": {
                        "ota update": {
                            "command": ["python", "ota_deploy.py"],
                            "interactive": True,
                        }
                    }
                }),
                encoding="utf-8",
            )
            router = self.make_router(config)
            with (
                patch("builtins.input", return_value="yes"),
                patch("voice_actions.shutil.which", return_value="python.exe"),
                patch("voice_actions.subprocess.Popen") as popen,
                patch("voice_actions.threading.Thread") as thread,
                patch("voice_actions.subprocess.CREATE_NEW_CONSOLE", 16,
                      create=True),
            ):
                router.handle("run action: ota update")

            self.assertEqual(popen.call_args.args[0], ("python.exe", "ota_deploy.py"))
            self.assertIn(
                "STICKS3_INTERACTION_DIR",
                popen.call_args.kwargs["env"],
            )
            self.assertTrue(
                Path(popen.call_args.kwargs["env"]["STICKS3_INTERACTION_DIR"]).is_dir()
            )
            self.assertEqual(popen.call_args.kwargs["creationflags"], 16)
            thread.assert_called_once()

    def test_interactive_action_route_opens_voice_aware_confirmation(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            config = Path(temporary) / "actions.json"
            config.write_text(
                json.dumps({
                    "actions": {
                        "ota update": {
                            "command": ["python", "ota_deploy.py"],
                            "interactive": True,
                        }
                    }
                }),
                encoding="utf-8",
            )
            router = self.make_router(config)
            with patch("voice_actions.threading.Thread") as thread:
                result = router.route("run action: ota update")

            self.assertIsNone(result)
            self.assertTrue(router._interactive_pending)
            self.assertIsNone(router._interactive_process)
            thread.assert_called_once()
            session = router._interactive_session
            self.assertIsNotNone(session)
            session.rmdir()

    def test_interactive_confirmation_is_a_popup_choice(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            session = Path(temporary)
            with patch("voice_actions.prompt_choice", return_value=1) as choose:
                confirmed = VoiceCommandRouter._confirm(
                    "action 'ota update'",
                    "Run this configured action?",
                    session_dir=session,
                )

            self.assertTrue(confirmed)
            self.assertEqual(
                choose.call_args.kwargs["voice_aliases"],
                ("no", "yes"),
            )
            self.assertEqual(choose.call_args.kwargs["session_dir"], session)

    def test_voice_is_forwarded_to_an_active_choice_popup(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            config = Path(temporary) / "missing.json"
            router = self.make_router(config)
            session = Path(temporary) / "session"
            session.mkdir()
            (session / "active_prompt.json").write_text(
                json.dumps({"kind": "choice"}),
                encoding="utf-8",
            )
            router._interactive_process = Mock()
            router._interactive_process.poll.return_value = None
            router._interactive_session = session
            self.assertTrue(router.has_active_prompt())

            result = router.handle("StickS3 BLE HID")

            self.assertIsNone(result)
            messages = [
                json.loads(line)
                for line in (session / "voice_inbox.jsonl").read_text(
                    encoding="utf-8"
                ).splitlines()
            ]
            self.assertEqual(messages, [{"text": "StickS3 BLE HID"}])

    def test_voice_is_buffered_while_action_confirmation_popup_starts(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            router = self.make_router(Path(temporary) / "missing.json")
            session = Path(temporary) / "session"
            session.mkdir()
            router._interactive_session = session
            router._interactive_pending = True

            self.assertTrue(router.has_active_prompt())
            self.assertIsNone(router.route("yes"))
            self.assertFalse((session / "voice_inbox.jsonl").exists())

            (session / "active_prompt.json").write_text(
                json.dumps({"kind": "choice"}),
                encoding="utf-8",
            )
            self.assertIsNone(router.route("yes"))
            messages = [
                json.loads(line)
                for line in (session / "voice_inbox.jsonl").read_text(
                    encoding="utf-8"
                ).splitlines()
            ]
            self.assertEqual(messages, [{"text": "yes"}])

    def test_interactive_action_swallows_speech_between_popups(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            router = self.make_router(Path(temporary) / "missing.json")
            session = Path(temporary) / "session"
            session.mkdir()
            router._interactive_session = session
            router._interactive_pending = True

            self.assertIsNone(router.route("please build the firmware"))
            self.assertFalse((session / "voice_inbox.jsonl").exists())
    def test_voice_is_not_forwarded_from_a_text_prompt(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            router = self.make_router(Path(temporary) / "missing.json")
            session = Path(temporary) / "session"
            session.mkdir()
            (session / "active_prompt.json").write_text(
                json.dumps({"kind": "text"}),
                encoding="utf-8",
            )
            router._interactive_process = Mock()
            router._interactive_process.poll.return_value = None
            router._interactive_session = session

            result = router.handle("password is secret")

            self.assertIsNone(result)
            self.assertFalse((session / "voice_inbox.jsonl").exists())

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
            self.assertEqual(run.call_args_list[1].kwargs["stderr"], subprocess.PIPE)
            self.assertTrue(run.call_args_list[1].kwargs["text"])

    def test_opencode_failure_includes_cli_stderr(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            router = self.make_router(Path(temporary) / "missing.json")
            router.opencode_session = "session-1"
            with (
                patch("builtins.input", return_value="yes"),
                patch("voice_actions.shutil.which", return_value="opencode.cmd"),
                patch(
                    "voice_actions.subprocess.run",
                    return_value=subprocess.CompletedProcess(
                        [], 1, stdout="", stderr="Provider authentication failed"
                    ),
                ),
            ):
                with self.assertRaisesRegex(
                    RuntimeError, "exit code 1: Provider authentication failed"
                ):
                    router.handle("OpenCode: summarize this project")

    def test_opencode_confirmation_uses_desktop_dialog_when_configured(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            with (
                patch.dict(
                    "os.environ",
                    {"STICKS3_INTERACTION_DIR": temporary},
                ),
                patch("voice_actions.prompt_choice", return_value=1) as choose,
            ):
                approved = VoiceCommandRouter._confirm(
                    "OpenCode session session-1",
                    "inspect this project",
                )

        self.assertTrue(approved)
        self.assertEqual(choose.call_args.kwargs["session_dir"], Path(temporary))

    def test_opencode_alias_uses_desktop_session_and_confirmation_dialogs(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            router = self.make_router(Path(temporary) / "missing.json")
            with (
                patch.dict(
                    "os.environ",
                    {"STICKS3_INTERACTION_DIR": temporary},
                ),
                patch(
                    "voice_actions.shutil.which",
                    return_value="opencode.cmd",
                ),
                patch(
                    "voice_actions.subprocess.run",
                    side_effect=(
                        subprocess.CompletedProcess(
                            [],
                            0,
                            stdout=json.dumps([{
                                "id": "session-1",
                                "title": "Project",
                                "directory": temporary,
                            }]),
                            stderr="",
                        ),
                        subprocess.CompletedProcess([], 0),
                    ),
                ) as run,
                patch(
                    "voice_actions.prompt_choice",
                    side_effect=(0, 1),
                ) as choose,
            ):
                router.route("opencode inspect this project")

        self.assertEqual(router.opencode_session, "session-1")
        self.assertEqual(choose.call_count, 2)
        self.assertIn("Project", choose.call_args_list[0].args[2][0])
        self.assertIn("creationflags", run.call_args_list[1].kwargs)

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
