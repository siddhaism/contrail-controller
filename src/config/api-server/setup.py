#!/usr/bin/env python
#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
import setuptools

def requirements(filename):
    with open(filename) as f:
        lines = f.read().splitlines()
    return lines

setuptools.setup(
    name = "contrail-config",
    version = "1.0",
    packages = setuptools.find_packages(),

    # metadata
    author = "OpenContrail",
    author_email = "dev@lists.opencontrail.org",
    license = "Apache Software License",
    url = "http://www.opencontrail.org/",

    install_requires = requirements('requirements.txt'),
)
