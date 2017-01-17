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
        self.assertEquals(self.props['_DurationDen'], 24)
        with self.assertRaises(KeyError):
            self.props['_NonExistent']

        with self.assertRaises(vs.Error):
            self.props['_DurationDen'] = 1

        with self.assertRaises(vs.Error):
            del self.props['_DurationDen']

        self.assertEquals(self.props['_DurationDen'], 24)

        self.assertEquals(self.props_rw['_DurationDen'], 24)
        self.props_rw['_DurationDen'] = 1
        self.assertEquals(self.props_rw['_DurationDen'], 1)
        del self.props_rw['_DurationDen']
        self.assertFalse('_DurationDen' in self.props_rw)

    def test_length(self):
        self.assertCountEqual(self.props, self.props_rw)
        self.assertEquals(len(self.props_rw), 2)
        del self.props_rw['_DurationDen']
        self.assertEquals(len(self.props_rw), 1)
        self.props_rw['_DurationDen'] = 1
        self.assertEquals(len(self.props_rw), 2)
        self.props_rw['TestEntry'] = "123"
        self.assertEquals(len(self.props_rw), 3)

    def test_iterators(self):
        self.assertEquals(list(self.props.keys()), list(self.props))
        self.assertEquals(list(self.props.keys()), ['_DurationDen', '_DurationNum'])
        self.assertEquals(list(self.props.values()), [24, 1])
        self.assertEquals(list(self.props.items()),  [('_DurationDen', 24), ('_DurationNum', 1)])
        self.assertEquals(dict(self.props), dict(self.props_rw))
        self.assertEquals(dict(self.props), {'_DurationDen': 24, '_DurationNum': 1})

    def test_get_pop(self):
        self.assertEquals(self.props.get('_DurationDen'), 24)
        self.assertEquals(self.props.get('_NonExistent'), None)
        self.assertEquals(self.props.get('_NonExistent', "Testificate"), "Testificate")

        with self.assertRaises(KeyError):
            self.props.pop("_NonExistent")

        x = []
        self.assert_(self.props.pop("_NonExistent", x) is x)
        self.assertEquals(self.props.pop("_NonExistent", None), None)
        self.assertEquals(self.props.pop("_NonExistent", "Testificate"), "Testificate")

        self.assertEquals(self.props_rw.pop("_DurationDen"), 24)
        with self.assertRaises(KeyError):
            self.props_rw.pop("_DurationDen")
        self.assertEquals(self.props_rw.pop("_DurationDen", "Test"), "Test")
        self.assertEquals(self.props_rw.popitem(), ("_DurationNum", 1))

    def test_setdefault(self):
        self.assertEquals(self.props_rw.setdefault("_DurationDen"), 24)
        self.assertFalse("_NonExistent1" in self.props_rw)
        self.assertEquals(self.props_rw.setdefault("_NonExistent1"), 0)
        self.assertEquals(self.props_rw["_NonExistent1"], 0)

        self.assertEquals(self.props_rw.setdefault("_NonExistent2", "Testificate"), b"Testificate")
        self.assertEquals(self.props_rw["_NonExistent2"], b"Testificate")

    def test_attr_access(self):
        self.assertEquals(self.props._DurationDen, 24)
        with self.assertRaises(AttributeError):
            self.props._NonExistent

        with self.assertRaises(vs.Error):
            self.props._DurationDen = 1

        with self.assertRaises(vs.Error):
            del self.props._DurationDen

        self.assertEquals(self.props._DurationDen, 24)

        self.assertEquals(self.props_rw._DurationDen, 24)
        self.props_rw._DurationDen = 1
        self.assertEquals(self.props_rw._DurationDen, 1)
        del self.props_rw._DurationDen
        self.assertFalse(hasattr(self.props_rw, '_DurationDen'))


if __name__ == '__main__':
    unittest.main()
