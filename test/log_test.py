import unittest
import vapoursynth as vs

class LogTestSequence(unittest.TestCase):

    def setUp(self):
        self.core = vs.core      
        self.handle = self.core.add_log_handler(lambda message_type, message : print(str(message_type) + ' ' + message))

        
    ##########################################################################
    # Tests start here

    def test_log(self):
        self.core.log_message(vs.MESSAGE_TYPE_INFORMATION, 'hello!')
        self.core.remove_log_handler(self.handle)
        self.handle = None
        self.core.log_message(vs.MESSAGE_TYPE_INFORMATION, 'not hello!')

if __name__ == '__main__':
    unittest.main()
