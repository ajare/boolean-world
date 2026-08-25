import pathlib
import subprocess
import sys
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "tools/migrate_proc_materials.py"
REGISTRY = ROOT / "src/BooleanWorld/common/include/common/MaterialRegistry.h"


class ProcMaterialMigrationTests(unittest.TestCase):
    def test_generates_catalog_rewrites_level_and_cleans_config(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            game = root / "Game.yaml"
            level = root / "world.yaml"
            output = root / "catalog.yaml"
            game.write_bytes(
                b"Configuration:\r\n"
                b"  # compiled material overrides\r\n"
                b"  Materials:\r\n"
                b"    - name: Marble\r\n"
                b"      params:\r\n"
                b"        - name: warp_scale\r\n"
                b"          default: 1.4\r\n"
                b"  Other: true\r\n"
            )
            legacy = (
                "world:\r\n"
                "  primitivePropertySet:\r\n"
                "    floorMaterial:\r\n"
                "      materialIndex: 0\r\n"
                "      materialDef:\r\n"
                "        params: [1.1, 6, 18, 0.15, 0.25, 0.65, 0.2, 0.5]\r\n"
                "        baseColour: [0.18, 0.18, 0.2]\r\n"
            )
            level.write_bytes(legacy.encode())

            subprocess.run(
                [
                    sys.executable,
                    str(SCRIPT),
                    "--registry", str(REGISTRY),
                    "--override-config", str(game),
                    "--level", str(level),
                    "--output", str(output),
                    "--clean-config", str(game),
                ],
                check=True,
                capture_output=True,
                text=True,
            )

            catalog = output.read_text()
            self.assertEqual(catalog.count("  - materialIndex:"), 37)
            self.assertIn('id: "builtin.marble"', catalog)
            self.assertIn("default: 1.4", catalog)
            self.assertIn('id: "migrated.marble.1"', catalog)
            self.assertIn("params: [1.1, 6.0, 18.0, 0.15, 0.25, 0.65, 0.2, 0.5]", catalog)

            migrated_level = level.read_bytes()
            self.assertIn(b'floorMaterial: "migrated.marble.1"\r\n', migrated_level)
            self.assertNotIn(b"materialIndex", migrated_level)
            self.assertEqual(game.read_bytes(), b"Configuration:\r\n  Other: true\r\n")


if __name__ == "__main__":
    unittest.main()
