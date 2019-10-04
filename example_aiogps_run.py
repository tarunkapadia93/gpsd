#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
Script that runs the aiogps example. Fails gracefully on unsupported
Python versions.
"""

# Copyright (c) 2019 Grand Joldes (grandwork2@yahoo.com). All rights reserved.
#
# This file is Copyright (c) 2019 by the GPSD project
#
# SPDX-License-Identifier: BSD-2-clause

# This code runs compatibly under Python 2 and 3.x for x >= 2.

import sys


if __name__ == "__main__":
    # aiogps only available on Python >= 3.6
    if sys.version_info[0] >= 3 and sys.version_info[1] >= 6:
        import example_aiogps
        example_aiogps.run()
    else:
        sys.exit("Sorry, aiogps is only available for Python versions >= 3.6")
