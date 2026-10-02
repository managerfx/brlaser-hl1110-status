import importlib.util
import pathlib
import unittest


TOOL_PATH = pathlib.Path(__file__).parents[1] / "tools" / "brlaser-hl1110-reset.py"
SPEC = importlib.util.spec_from_file_location("brlaser_hl1110_reset", TOOL_PATH)
RESET_TOOL = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(RESET_TOOL)


class ResetToolTest(unittest.TestCase):
    def test_pjl_frame_uses_separate_final_escape(self):
        frame = RESET_TOOL.pjl_frame("INFO STATUS")
        self.assertEqual(
            frame,
            b"\x1b%-12345X@PJL\r\n@PJL INFO STATUS\r\n\x1b%-12345X",
        )
        self.assertEqual(RESET_TOOL.FINAL_ESCAPE, b"\x1b")

    def test_extracts_last_pjl_value(self):
        response = "PAGECOUNT=3\r\nDISPLAY=\"SLEEP\"\r\nPAGECOUNT=4\r\n"
        self.assertEqual(RESET_TOOL.pjl_value(response, "PAGECOUNT"), "4")
        self.assertEqual(RESET_TOOL.pjl_value(response, "DISPLAY"), "SLEEP")
        self.assertEqual(RESET_TOOL.pjl_value(response, "CODE"), "")

    def test_accepts_only_the_expected_printer_identity(self):
        identity = "MFG:Brother;CMD:PJL,HBP;MDL:HL-1110 series;CLS:PRINTER;"
        self.assertTrue(RESET_TOOL.valid_identity(identity))
        self.assertFalse(RESET_TOOL.valid_identity(identity.replace("HL-1110", "HL-1210")))
        self.assertFalse(RESET_TOOL.valid_identity(identity.replace("Brother", "Other")))


if __name__ == "__main__":
    unittest.main()