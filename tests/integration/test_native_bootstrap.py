"""Opt-in native integration checks; the executable contains the user's local DOL."""
import os
import subprocess
import tempfile
import unittest


@unittest.skipUnless(os.environ.get("KIRBY_NATIVE_TEST"), "Set KIRBY_NATIVE_TEST to the locally built executable")
class NativeBootstrapTests(unittest.TestCase):
    def invoke(self, option):
        return subprocess.run([os.environ["KIRBY_NATIVE_TEST"], option], capture_output=True, text=True, timeout=15)

    def test_foreign_native_registration_cannot_replace_kirby_entry(self):
        result = self.invoke("--verify-bootstrap")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("foreign native override rejected", result.stdout)
        self.assertIn("foreign translated override rejected", result.stdout)
        self.assertIn("Kirby translated entry resolved", result.stdout)
        self.assertIn("interrupt restore ABI verified", result.stdout)
        self.assertIn("native cache boundaries verified", result.stdout)

    def test_headless_graphics_stops_before_using_an_uninitialized_render_target(self):
        result = self.invoke("--headless")
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("GX scissor requires initialized Aurora; run --video", result.stderr)
        self.assertIn("GX stream: packets=", result.stderr)
        self.assertNotIn("Signal 11", result.stdout + result.stderr)

    def test_compiled_game_register_initializer_sets_the_verified_sda_bases(self):
        result = self.invoke("--probe-init")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("r2=0x808ee8e0 r13=0x808e73e0", result.stdout)
        self.assertIn("stack is inside MEM1", result.stdout)

    def test_original_video_waits_resume_through_sdk_irq_and_thread_queues(self):
        result = self.invoke("--probe-video-retrace")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("three original VI waits resumed through the original interrupt handler", result.stdout)
        self.assertIn("CPU state and SDK queues preserved", result.stdout)

    def test_wpad_bridge_serializes_reports_and_defers_guest_callbacks(self):
        result = self.invoke("--probe-wpad")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("WPAD native bridge: reports, disconnect, sampling buffer and masked callbacks verified", result.stdout)

    def test_sdl_controller_events_reach_guest_reports_and_release_on_disconnect(self):
        # Port assignment is persistent in Aurora: keep the probe's virtual device
        # and mapping files separate from the user's controller preferences.
        executable = os.path.abspath(os.environ["KIRBY_NATIVE_TEST"])
        data = os.path.abspath("disc/DATA")
        with tempfile.TemporaryDirectory(prefix="kirby-sdl-input-") as directory:
            result = subprocess.run(
                [executable, "--probe-sdl-controller", "--game-data", data],
                cwd=directory, capture_output=True, text=True, timeout=15)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("SDL controller events: directions, face buttons, releases and disconnect verified", result.stdout)
        for preset in ("standard", "PS5", "Xbox 360", "Xbox One"):
            with self.subTest(preset=preset):
                self.assertIn(f"SDL preset verified: Kirby {preset} probe", result.stdout)

    def test_original_threads_sleep_wake_exit_and_preserve_native_continuations(self):
        result = self.invoke("--probe-threads")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("original SDK threads: sleep, wake, exit and CPU state verified", result.stdout)

    def test_worker_exception_returns_to_the_root_stack_for_diagnostics(self):
        result = self.invoke("--probe-thread-fault")
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("WPAD output exceeds guest RAM", result.stderr)
        self.assertNotIn("Signal 11", result.stdout + result.stderr)

    def test_original_ax_task_boot_delivers_irq_and_mixes_samples(self):
        result = self.invoke("--probe-ax-task")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("original AX task boot and IRQ callback returned; 96 stereo samples mixed; task replies verified", result.stdout)
        self.assertIn("retail MEM2 sample bounds and PCM voice stereo output verified", result.stdout)
        self.assertIn("pending DSP reply delivered at interrupt-enable boundary without scheduler idle", result.stdout)

    def test_original_sleep_alarms_resume_and_respect_masks_and_cancellation(self):
        result = self.invoke("--probe-alarms")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("three original sleep alarms resumed; deadline, mask, cancellation and SDK context verified", result.stdout)

    def test_argument_failure_does_not_shutdown_an_uninitialized_renderer(self):
        result = self.invoke("--invalid-option")
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertNotIn("Signal 11", result.stdout + result.stderr)

    def test_invalid_volume_is_rejected_before_startup(self):
        for volume in ("nan", "inf", "-1", "101", "invalid", ""):
            with self.subTest(volume=volume):
                result = subprocess.run([os.environ["KIRBY_NATIVE_TEST"], "--volume", volume],
                                        capture_output=True, text=True, timeout=15)
                self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
                self.assertIn("--volume requires a number from 0 to 100", result.stderr)

    def test_game_interrupt_initializer_installs_its_handler_and_masks_sources(self):
        result = self.invoke("--probe-interrupt-init")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("Kirby interrupt initializer returned", result.stdout)
        self.assertIn("external handler installed; device sources masked", result.stdout)

    def test_game_exi_initializer_completes_absent_accessory_identification(self):
        result = self.invoke("--probe-exi-init")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("Kirby EXI initializer returned; diagnostic output delivered", result.stdout)
        self.assertIn("[kirby:EUART]", result.stderr)

    def test_game_si_initializer_configures_polling(self):
        result = self.invoke("--probe-si-init")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("Kirby SI initializer returned; polling configured", result.stdout)

    def test_game_sram_initializer_synchronizes_persistent_data(self):
        result = self.invoke("--probe-sram-init")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("Kirby SRAM initializer returned; 64 bytes synchronized", result.stdout)

    def test_boot_memory_uses_retail_aliases_disc_metadata_and_ios56(self):
        result = self.invoke("--probe-boot-memory")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("boot memory verified; MEM2 aliases and boundaries", result.stdout)
        self.assertIn("60.75 MHz timebase", result.stdout)
        self.assertIn("Kirby IPC buffer initialization returned", result.stdout)

    def test_audio_clock_runs_the_original_sdk_sequence_and_delays(self):
        result = self.invoke("--probe-audio-clock")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("both modes and settling delays verified; DSP still uninitialized", result.stdout)

    def test_audio_system_completes_dsp_boot_and_restores_work_memory(self):
        result = self.invoke("--probe-dsp-init")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("two ARAM DMAs; verified DSP init mail; work buffer restored", result.stdout)

    def test_ipc_client_initializes_heap_and_interrupt_handler(self):
        result = self.invoke("--probe-ipc-client")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("8192-byte heap; handler and mailbox masks installed", result.stdout)

    def test_stm_initializes_and_reuses_the_original_ipc_heap(self):
        result = self.invoke("--probe-stm-init")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("owned descriptors; pending event; 256 open/close cycles through original SDK heap", result.stdout)
        self.assertIn("original callback completed; caller context preserved; request freed", result.stdout)

    def test_system_configuration_loads_and_parses_local_nand(self):
        result = self.invoke("--probe-system-config")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("Kirby system configuration loaded; parsed items=", result.stdout)
        self.assertIn("original NAND/ES wrappers and async callbacks", result.stdout)
        self.assertIn("IOS callback delivery follows guest interrupt enable", result.stdout)

    def test_disc_reads_use_original_sdk_callbacks_and_extracted_bytes(self):
        result = self.invoke("--probe-disc")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("Kirby DVD low initializer and 16 reads completed", result.stdout)
        self.assertIn("disc ID, FST and banner bytes verified; original callbacks and context reuse", result.stdout)
        self.assertIn("DVD completions delivered at transport return when enabled, deferred when masked", result.stdout)

    def test_es_metadata_uses_extracted_ticket_and_tmd_through_original_sdk(self):
        result = self.invoke("--probe-es-metadata")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("original ESP ticket views and TMD queries match extracted metadata", result.stdout)
        self.assertIn("unlimited-ticket consumption verified; library closed", result.stdout)

    def test_original_av_encoder_i2c_writes_reach_the_gpio_device(self):
        result = self.invoke("--probe-ave")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("two register payloads, six ACKs; stack and interrupt state preserved", result.stdout)


if __name__ == "__main__":
    unittest.main()
