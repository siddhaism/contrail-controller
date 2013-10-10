import os
import sys

testdir = os.path.dirname(__file__)
topdir = os.path.abspath(os.path.join(testdir, '../../../../../'))

sys.path.insert(0, os.path.join(topdir, 'build/debug/config/api-server/ut-venv/lib/python2.7/site-packages'))
sys.path.append(os.path.join(testdir, '../../utils'))

from provision_bgp import BgpProvisioner

def main():
    provisioner = BgpProvisioner("admin", "c0ntrail123", "admin",
                                 "127.0.0.1", 50000)
    provisioner.add_bgp_router('contrail', 'cn-test', '192.168.1.1', 64512)
    provisioner.add_bgp_router('mx', 'mx-test', '192.168.1.2', 64512)

if __name__ == '__main__':
    main()
