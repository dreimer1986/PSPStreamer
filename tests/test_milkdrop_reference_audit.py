import unittest

from tools.audit_milkdrop_import import inventory


class ReferenceAuditTests(unittest.TestCase):
    def test_literal_and_constructed_fields(self):
        rows = inventory('''
m_fWaveAlpha = GetFastFloat("fWaveAlpha", m_fWaveAlpha.eval(-1), f);
sprintf(buf, "shapecode_%d_%s", i, "num_inst" ); instances = GetFastInt(buf, instances, f2);
sprintf(buf, "wavecode_%d_%s", i, "scaling" ); scaling = GetFastFloat(buf, scaling, f2);
''')
        self.assertEqual([r['field'] for r in rows],
                         ['fWaveAlpha', 'shapecode_{slot}_num_inst', 'wavecode_{slot}_scaling'])
        self.assertEqual([r['type'] for r in rows], ['Float', 'Int', 'Float'])
        self.assertEqual([r['line'] for r in rows], [2, 3, 4])
        self.assertEqual(rows[1]['default_expression'], 'instances')

    def test_does_not_infer_editor_bounds(self):
        self.assertEqual(inventory('AddItem("Wave alpha", .001f, 100);'), [])


if __name__ == '__main__':
    unittest.main()
