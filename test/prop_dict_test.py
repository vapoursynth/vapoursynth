import unittest
import vapoursynth as vs


class PropDictTest(unittest.TestCase):
    def setUp(self):
        self.core = vs.get_core()
        self.frame = self.core.std.BlankClip().get_frame(0)
        self.props = self.frame.props

        self.frame_copy = self.frame.copy()
        self.props_rw = self.frame_copy.props

    def test_item_access(self):
        self.assertEqual(self.props['_DurationDen'], 24)
        with self.assertRaises(KeyError):
            self.props['_NonExistent']

        with self.assertRaises(vs.Error):
            self.props['_DurationDen'] = 1

        with self.assertRaises(vs.Error):
            del self.props['_DurationDen']

        self.assertEqual(self.props['_DurationDen'], 24)

        self.assertEqual(self.props_rw['_DurationDen'], 24)
        self.props_rw['_DurationDen'] = 1
        self.assertEqual(self.props_rw['_DurationDen'], 1)
        del self.props_rw['_DurationDen']
        self.assertFalse('_DurationDen' in self.props_rw)

    def test_length(self):
        self.assertCountEqual(self.props, self.props_rw)
        self.assertEqual(len(self.props_rw), 2)
        del self.props_rw['_DurationDen']
        self.assertEqual(len(self.props_rw), 1)
        self.props_rw['_DurationDen'] = 1
        self.assertEqual(len(self.props_rw), 2)
        self.props_rw['TestEntry'] = "123"
        self.assertEqual(len(self.props_rw), 3)

    def test_iterators(self):
        self.assertEqual(list(self.props.keys()), list(self.props))
        self.assertEqual(list(self.props.keys()), ['_DurationDen', '_DurationNum'])
        self.assertEqual(list(self.props.values()), [24, 1])
        self.assertEqual(list(self.props.items()),  [('_DurationDen', 24), ('_DurationNum', 1)])
        self.assertEqual(dict(self.props), dict(self.props_rw))
        self.assertEqual(dict(self.props), {'_DurationDen': 24, '_DurationNum': 1})

    def test_get_pop(self):
        self.assertEqual(self.props.get('_DurationDen'), 24)
        self.assertEqual(self.props.get('_NonExistent'), None)
        self.assertEqual(self.props.get('_NonExistent', "Testificate"), "Testificate")

        with self.assertRaises(KeyError):
            self.props.pop("_NonExistent")

        x = []
        self.assertTrue(self.props.pop("_NonExistent", x) is x)
        self.assertEqual(self.props.pop("_NonExistent", None), None)
        self.assertEqual(self.props.pop("_NonExistent", "Testificate"), "Testificate")

        self.assertEqual(self.props_rw.pop("_DurationDen"), 24)
        with self.assertRaises(KeyError):
            self.props_rw.pop("_DurationDen")
        self.assertEqual(self.props_rw.pop("_DurationDen", "Test"), "Test")
        self.assertEqual(self.props_rw.popitem(), ("_DurationNum", 1))

    def test_setdefault(self):
        self.assertEqual(self.props_rw.setdefault("_DurationDen"), 24)
        self.assertFalse("_NonExistent1" in self.props_rw)
        self.assertEqual(self.props_rw.setdefault("_NonExistent1"), 0)
        self.assertEqual(self.props_rw["_NonExistent1"], 0)

        self.assertEqual(self.props_rw.setdefault("_NonExistent2", "Testificate"), b"Testificate")
        self.assertEqual(self.props_rw["_NonExistent2"], b"Testificate")

    def test_attr_access(self):
        self.assertEqual(self.props._DurationDen, 24)
        with self.assertRaises(AttributeError):
            self.props._NonExistent

        with self.assertRaises(vs.Error):
            self.props._DurationDen = 1

        with self.assertRaises(vs.Error):
            del self.props._DurationDen

        self.assertEqual(self.props._DurationDen, 24)

        self.assertEqual(self.props_rw._DurationDen, 24)
        self.props_rw._DurationDen = 1
        self.assertEqual(self.props_rw._DurationDen, 1)
        del self.props_rw._DurationDen
        self.assertFalse(hasattr(self.props_rw, '_DurationDen'))


if __name__ == '__main__':
    unittest.main()
