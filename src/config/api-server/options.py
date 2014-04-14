import argparse
import ConfigParser
import itertools
import os
import pdb
import sys

# from pysandesh.sandesh_base import *
# from pysandesh.gen_py.sandesh.ttypes import SandeshLevel

command_sections = [
    'DEFAULT',
    'DISCOVERY',
    'KEYSTONE',
    'IFMAP',
    'RABBIT',
    'REDIS',
    'SECURITY',
]

_WEB_HOST = '0.0.0.0'
_WEB_PORT = 8082

def groupargs(arg, currentarg=[None]):
    global command_sections;
    if (arg in command_sections):currentarg[0] = arg
    return currentarg[0]

class Options(object):
    def __init__(self):
        # Intialize default value of various options supported.

        # DEFAULT section options
        self.options = { }
        self.options['DEFAULT'] = {
            'auth' : 'keystone',
            'cassandra_server_list' : '127.0.0.1:9160',
            'collectors': '127.0.0.1:8086',
            'http_server_port': '8084',
            'listen_ip_addr': _WEB_HOST,
            'listen_port': _WEB_PORT,
            'log_category': '',
            'log_level': 7, # SandeshLevel.SYS_DEBUG,
            'log_local': False,
            'log_file': '<stdout>', # Sandesh._DEFAULT_LOG_FILE,
            'logging_level': 'WARN',
            'multi_tenancy': False,
            'reset_config': False,
            'wipe_config': False,
            'worker_id': '0',
            'zk_server_ip': '127.0.0.1:2181',
        }

        # DISCOVERY section options
        self.options['DISCOVERY'] = {
            'port': 5998,
            'server': "127.0.0.1",
        }

        # RABBIT section options
        self.options['RABBIT'] = {
            'password': 'guest',
            'server': 'localhost',
            'user': 'guest',
            'vhost': None,
        }

        # IFMAP section options
        self.options['IFMAP'] = {
            'port': 8443,
            'server': "127.0.0.1",
            'user': "api-server",
            'password': "api-server",
            'server_url': "",
        }

        # REDIS section options
        self.options['REDIS'] = {
            'password': "ap-server",
            'port': 8443,
            'server': "127.0.0.1",
            'server_url': "https://127.0.0.1:8443",
            'user': "ap-server",
        }

        # SECURITY section options
        self.options['SECURITY'] = {
            'use_certs': False,
            'keyfile': '/etc/contrail/ssl/private_keys/apiserver_key.pe',
            'certfile': '/etc/contrail/ssl/certs/apiserver.pem',
            'ca_certs': '/etc/contrail/ssl/certs/ca.pem',
            'ifmap_certauth_port': "8444",
        }

        # KEYSTONE section options
        self.options['KEYSTONE'] = {
            'auth_host': '127.0.0.1',
            'auth_port': '35357',
            'auth_protocol': 'http',
            'admin_user': 'admin',
            'admin_password': 'admin',
            'admin_tenant_name': 'admin',
            'memcache_servers' : '',
            'token_cache_time' : '',
        }

    def get_options_dict(self):
        return self.options

    def add_section_security(self, subparsers):
        parser = subparsers.add_parser('SECURITY')
        parser.set_defaults(**self.options['SECURITY'])

        parser.add_argument('--use_certs', action='store_true',
                            help='User certificates')
        parser.add_argument('--keyfile', help='Key file')
        parser.add_argument('--certfile', help='Certificates file')
        parser.add_argument('--ca_certs', help='Certificates authority')
        parser.add_argument('--ifmap_certauth_port', help='IFMAP ca port')

    def add_section_keystone(self, subparsers):
        parser = subparsers.add_parser('KEYSTONE')
        parser.set_defaults(**self.options['KEYSTONE'])

        parser.add_argument('--auth_host', help='auth host')
        parser.add_argument('--auth_port', help='auth host')
        parser.add_argument('--auth_protocol', help='auth host')
        parser.add_argument('--admin_user', help='auth host')
        parser.add_argument('--admin_password', help='auth host')
        parser.add_argument('--admin_tenant_name', help='auth host')

    def add_section_ifmap(self, subparsers):
        parser = subparsers.add_parser('IFMAP')
        parser.set_defaults(**self.options['IFMAP'])

        parser.add_argument("--server", help="IP address of ifmap server")
        parser.add_argument("--port", help="Port of ifmap server")

        # TODO should be from certificate
        parser.add_argument("--user", help="Username known to ifmap server")
        parser.add_argument("--password", help="Password known to ifmap server")
        parser.add_argument("--server_url", help="Location of IFMAP server")

    def add_section_redis(self, subparsers):
        parser = subparsers.add_parser('REDIS')
        parser.set_defaults(**self.options['REDIS'])

        parser.add_argument("--server", help="IP address of redis server")
        parser.add_argument("--port", help="Port of redis server")

    def add_section_discovery(self, subparsers):
        parser = subparsers.add_parser('DISCOVERY')
        parser.set_defaults(**self.options['DISCOVERY'])

        parser.add_argument("--server", help="IP address of discovery server")
        parser.add_argument("--port", help="Port of discovery server")

    def add_section_rabbit(self, subparsers):
        parser = subparsers.add_parser('RABBIT')
        parser.set_defaults(**self.options['RABBIT'])

        parser.add_argument("--server", help="Rabbitmq server address")
        parser.add_argument("--user", help="Username for rabbit")
        parser.add_argument("--vhost", help="vhost for rabbit")
        parser.add_argument("--password", help="password for rabbit")

    def add_section_default(self, subparsers):
        parser = subparsers.add_parser('DEFAULT')
        parser.set_defaults(**self.options['DEFAULT'])

        parser.add_argument(
            "--cassandra_server_list",
            help="List of cassandra servers in IP Address:Port format",
            nargs='+')
        parser.add_argument(
            "--auth", choices=['keystone'],
            help="Type of authentication for user-requests")
        parser.add_argument(
            "--reset_config", action="store_true",
            help="Warning! Destroy previous configuration and start clean")
        parser.add_argument(
            "--wipe_config", action="store_true",
            help="Warning! Destroy previous configuration")
        parser.add_argument(
            "--listen_ip_addr",
            help="IP address to provide service on, default %s" % (_WEB_HOST))
        parser.add_argument(
            "--listen_port",
            help="Port to provide service on, default %s" % (_WEB_PORT))
        parser.add_argument(
            "--collectors",
            help="List of VNC collectors in ip:port format",
            nargs="+")
        parser.add_argument(
            "--http_server_port",
            help="Port of local HTTP server")
        parser.add_argument(
            "--log_local", action="store_true",
            help="Enable local logging of sandesh messages")
        parser.add_argument(
            "--log_level",
            help="Severity level for local logging of sandesh messages")
        parser.add_argument("--logging_level", help=("Log level for python logging: DEBUG, INFO, WARN, ERROR default: %s"
                  % self.options['DEFAULT']['logging_level']))
        parser.add_argument(
            "--log_category",
            help="Category filter for local logging of sandesh messages")
        parser.add_argument(
            "--log_file",
            help="Filename for the logs to be written to")
        parser.add_argument(
            "--multi_tenancy", action="store_true",
            help="Validate resource permissions (implies token validation)")
        parser.add_argument(
            "--worker_id",
            help="Worker Id")
        parser.add_argument(
            "--zk_server_ip",
            help="Ip address:port of zookeeper server")

    def _parse_args(self, args_str):

        # Source any specified config/ini file
        # Turn off help, so we print all options in response to -h
        conf_parser = argparse.ArgumentParser(add_help=False)
        conf_parser.add_argument("-c", "--conf_file",
                                 default='/etc/contrail/api_server.conf',
                                 help="Specify config file", metavar="FILE")
        args, remaining_argv = conf_parser.parse_known_args(args_str.split())

        if os.path.isfile(args.conf_file):
            config = ConfigParser.SafeConfigParser({'admin_token': None})
            config.read([args.conf_file])

            self.options['DEFAULT'].update(dict(config.items("DEFAULT")))
            self.options['DISCOVERY'].update(dict(config.items("DISCOVERY")))
            self.options['RABBIT'].update(dict(config.items("RABBIT")))
            self.options['REDIS'].update(dict(config.items("REDIS")))
            self.options['IFMAP'].update(dict(config.items("IFMAP")))
            self.options['SECURITY'].update(dict(config.items("SECURITY")))
            self.options['KEYSTONE'].update(dict(config.items("KEYSTONE")))

        # Override with CLI options
        # Don't surpress add_help here so it will handle -h
        parser = argparse.ArgumentParser(
            # Inherit options from config_parser
            parents=[conf_parser],
            # print script description with -h/--help
            description=__doc__,
            # Don't mess with format of description
            formatter_class=argparse.RawDescriptionHelpFormatter,
        )

        subparsers = parser.add_subparsers()
        self.add_section_security(subparsers)
        self.add_section_keystone(subparsers)
        self.add_section_default(subparsers)
        self.add_section_redis(subparsers)
        self.add_section_ifmap(subparsers)
        self.add_section_rabbit(subparsers)
        self.add_section_discovery(subparsers)

        commandlines = [
            list(args) for cmd, args in itertools.groupby(remaining_argv,
                                                          groupargs)
        ]

        for cmdline in commandlines:
            _args = parser.parse_args(cmdline)
            self.options[cmdline[0]].update(vars(_args))

        if type(self.options['DEFAULT']['cassandra_server_list']) is str:
            self.options['DEFAULT']['cassandra_server_list'] =\
                self.options['DEFAULT']['cassandra_server_list'].split()
        if type(self.options['DEFAULT']['collectors']) is str:
            self.options['DEFAULT']['collectors'] =\
                self.options['DEFAULT']['collectors'].split()

    # end _parse_args
# class Options

if __name__ == "__main__":
    opt = Options()
    opt._parse_args(" ".join(sys.argv[1:]))
    print opt.get_options_dict()

