import options
import subprocess

class OptionsTest():
    def __init__(self):
        self.cmd_options = options.Options()

    def test1(self):
        opts = self.cmd_options.get_options_dict()
        assert(opts['DEFAULT']['listen_port'] == 8082)
        self.cmd_options._parse_args("DEFAULT --listen_port 200")
        assert(opts['DEFAULT']['listen_port'] == "200")

    def test2(self):
        opts = self.cmd_options.get_options_dict()
        assert(opts['DISCOVERY']['port'] == 5998)
        self.cmd_options._parse_args("DISCOVERY --port 500")
        assert(opts['DISCOVERY']['port'] == "500")

    # Test options overridden from multiple sections
    def test3(self):
        opts = self.cmd_options.get_options_dict()
        assert(opts['DEFAULT']['reset_config'] == False)
        assert(opts['REDIS']['port'] == 8443)

        self.cmd_options._parse_args("REDIS --port 800 DEFAULT --reset_config")
        assert(opts['DEFAULT']['reset_config'] == True)
        assert(opts['REDIS']['port'] == "800")

    # Load default config file (all comments)
    def test4(self):
        opts = self.cmd_options.get_options_dict()
        assert(opts['DEFAULT']['reset_config'] == False)
        self.cmd_options._parse_args("--conf_file api-server.conf")
        assert(opts['DEFAULT']['reset_config'] == False)

    # Load a new config file
    def test5(self):
        opts = self.cmd_options.get_options_dict()
        conf = '''
[DEFAULT]
reset_config = True
[DISCOVERY]
port = 6000
[KEYSTONE]
[IFMAP]
[RABBIT]
[REDIS]
[SECURITY]
'''
        # Write conf to a temp file
        conf_file = "/tmp/foo.conf"
        with open(conf_file, "w") as fp:
            fp.write(conf)

        assert(opts['DEFAULT']['reset_config'] == False)
        assert(opts['DISCOVERY']['port'] == 5998)
        self.cmd_options._parse_args("--conf_file " + conf_file)
        assert(opts['DEFAULT']['reset_config'] == "True")
        assert(opts['DISCOVERY']['port'] == "6000")

    # Load a new config file and override some values from command line
    def test6(self):
        opts = self.cmd_options.get_options_dict()
        conf = '''
[DEFAULT]
reset_config = True
[DISCOVERY]
port = 6000
[KEYSTONE]
[IFMAP]
[RABBIT]
[REDIS]
[SECURITY]
'''
        # Write conf to a temp file
        conf_file = "/tmp/foo.conf"
        with open(conf_file, "w") as fp:
            fp.write(conf)

        assert(opts['DEFAULT']['reset_config'] == False)
        assert(opts['DISCOVERY']['port'] == 5998)
        self.cmd_options._parse_args("--conf_file " + conf_file + \
                                     " DISCOVERY --port 8000")
        assert(opts['DEFAULT']['reset_config'] == "True")
        assert(opts['DISCOVERY']['port'] == "8000")

    # Test help string
    def test7(self):
        subprocess.check_output("python options.py --help", shell=True)

if __name__ == "__main__":
    OptionsTest().test1()
    OptionsTest().test2()
    OptionsTest().test3()
    OptionsTest().test4()
    OptionsTest().test5()
    OptionsTest().test6()
    OptionsTest().test7()

