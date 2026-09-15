import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]

class PresetCatalogTests(unittest.TestCase):
    def test_browser_select_cancel_and_remote_stop(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory); (root/'presets').mkdir()
            for name in ('Alpha.milk','Zulu.milk'): (root/'presets'/name).touch()
            binary=root/'browser'
            subprocess.run(['cc','-Wall','-Wextra','-Werror','-I',str(ROOT/'psp-client'),
                            str(ROOT/'tests/preset_browser_harness.c'),'-o',str(binary)],check=True)
            subprocess.run([str(binary)],cwd=root,check=True,timeout=5)

    def test_catalog_filters_sorts_and_reports_limit(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            for name in ('z.milk','Alpha.MILK','ignore.txt'): (root/name).touch()
            (root/'directory.milk').mkdir()
            source=root/'test.c'
            source.write_text('''
#include <assert.h>
#include "preset_catalog.h"
int main(int argc,char **argv) {
    PresetCatalog c; assert(argc==2);
    assert(!preset_name_valid("../bad.milk"));
    assert(!preset_name_valid("line\\n.milk"));
    assert(preset_catalog_load(&c,argv[1]));
    if(c.truncated) { assert(c.count==128); return 0; }
    assert(c.count==2); assert(!strcmp(c.names[0],"Alpha.MILK"));
    assert(!strcmp(c.names[1],"z.milk"));
    assert(!preset_catalog_load(&c,"/path/that/does/not/exist"));
    assert(!c.count); return 0;
}
''')
            binary=root/'test'
            subprocess.run(['cc','-Wall','-Wextra','-Werror','-I',str(ROOT/'psp-client'),str(source),'-o',str(binary)],check=True)
            subprocess.run([str(binary),directory],check=True)
            for i in range(130): (root/f'{i}.milk').touch()
            subprocess.run([str(binary),directory],check=True)
