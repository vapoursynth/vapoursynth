import unittest
import vapoursynth as vs

class LogTestSequence(unittest.TestCase):

    def test_log(self):
        last_message = [None]
        def handler(message_type, message):
            last_message[0] = (message_type, message)

        handle = vs.core.add_log_handler(handler)
        vs.core.log_message(vs.MESSAGE_TYPE_INFORMATION, 'hello!')
        vs.core.remove_log_handler(handle)
        handle = None
        vs.core.log_message(vs.MESSAGE_TYPE_INFORMATION, 'not hello!')

        self.assertEqual(last_message[0], (vs.MESSAGE_TYPE_INFORMATION, 'hello!'))

if __name__ == '__main__':
    unittest.main()
