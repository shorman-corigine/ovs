..
      Licensed under the Apache License, Version 2.0 (the "License"); you may
      not use this file except in compliance with the License. You may obtain
      a copy of the License at

          http://www.apache.org/licenses/LICENSE-2.0

      Unless required by applicable law or agreed to in writing, software
      distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
      WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
      License for the specific language governing permissions and limitations
      under the License.

      Convention for heading levels in Open vSwitch documentation:

      =======  Heading 0 (reserved for the title in a document)
      -------  Heading 1
      ~~~~~~~  Heading 2
      +++++++  Heading 3
      '''''''  Heading 4

      Avoid deeper levels because they do not render well.

====================================
Distributions packaging Open vSwitch
====================================

This document lists various popular distributions packaging Open vSwitch.
Open vSwitch is packaged by various distributions for multiple platforms and
architectures.

.. note::
  The packaged version available with distributions may not be latest
  Open vSwitch release.

Debian / Ubuntu
---------------

You can use ``apt-get`` or ``aptitude`` to install the .deb packages and must
be superuser.

1. Debian and Ubuntu has ``openvswitch-switch`` and ``openvswitch-common``
packages that includes the core userspace components of the switch.  Extra
packages for documentation, ipsec, pki, VTEP and Python support are also
available.  The Open vSwitch kernel datapath is maintained as part of the
upstream kernel available in the distribution.

2. For fast userspace switching, Open vSwitch with DPDK support is
bundled in the package ``openvswitch-switch-dpdk``.

Fedora
------

Fedora provides ``openvswitch``, ``openvswitch-devel``, ``openvswitch-test``
and ``openvswitch-debuginfo`` rpm packages. You can install ``openvswitch``
package in minimum installation. Use ``yum`` or ``dnf`` to install the rpm
packages and must be superuser.

Red Hat
-------

RHEL distributes ``openvswitch`` rpm package that supports kernel datapath.
DPDK accelerated Open vSwitch can be installed using ``openvswitch-dpdk``
package.

OpenSuSE
--------

OpenSUSE provides ``openvswitch``, ``openvswitch-switch`` rpm packages. Also
``openvswitch-dpdk`` and ``openvswitch-dpdk-switch`` can be installed for
Open vSwitch using DPDK accelerated datapath.
