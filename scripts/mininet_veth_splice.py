#!/usr/bin/python

from mininet.topo import Topo
from mininet.node import CPULimitedHost
from mininet.link import TCLink
from mininet.net import Mininet
from mininet.log import lg, info
from mininet.util import dumpNodeConnections
from mininet.cli import CLI

import time
import os
import sys, getopt
import math
from subprocess import Popen, PIPE, call

class NetworkTopo(Topo):

    def __init__(self):
        super(NetworkTopo, self).__init__()

        r1 = self.addHost('r1')
        h1 = self.addHost('h1')
        h2 = self.addHost('h2')
        m1 = self.addHost('m1')
        h3 = self.addHost('h3')

        self.addLink(h1, r1, bw=1000, delay='2ms') 
        self.addLink(h2, r1, bw=1000, delay='2ms') 
        self.addLink(m1, r1, bw=1000, delay='20ms') 
        self.addLink(m1, r1, bw=1000, delay='20ms') 
        self.addLink(h3, r1, bw=1000, delay='20ms') 

        return


def startTCPprobe(outfile):
    os.system("rmmod tcp_probe; modprobe tcp_probe port=5001") 
    Popen("cat /proc/net/tcpprobe > %s" % outfile, shell=True)

def stopTCPprobe():
    Popen("killall -9 cat", shell=True).wait()
    
    
def configIntGW(net, node, dev, ip, gw, cmd):
    h = net.getNodeByName(node)
    h.cmd('ifconfig %s %s netmask 255.255.255.0 up' % (dev, ip))
    h.cmd('route add -net 0.0.0.0 gw %s'  % gw)
    h.cmd(cmd)
    return h

def configInt(net, node, dev, ip, cmd):
    h = net.getNodeByName(node)
    h.cmd('ifconfig %s %s netmask 255.255.255.0 up' % (dev, ip))
    h.cmd(cmd)
    return h

def run(splice_time, total_time):
    # Depending on the version of Mininet: before running an experiment,
    # do not forget to run the command below
    call(["mount", "--make-rprivate", "/"])
    # I tested with Mininet version 2.2.1 and Linux kernel 4.1.6
    
    call(["mn", "-c"])
    call(["modprobe", "veth"])  # This line is for Dysco only
    call(["rmmod", "veth"])

    time.sleep(1)
    call(["insmod", "veth.ko"])

    time.sleep(2)
    
    call(["sysctl", "net.ipv4.tcp_congestion_control=reno"])
    #call(["sysctl", "net.ipv4.tcp_sack=1"])

    print("It will build the topology\n")
    
    topo = NetworkTopo()

    net = Mininet(topo=topo, host=CPULimitedHost, link=TCLink, controller=None) 

    time.sleep(1)
    print("Starting the network ...\n")        
    net.start()

    r1 = net.getNodeByName('r1')
    r1.cmd('ifconfig r1-eth0 10.0.1.1 netmask 255.255.255.0 up')
    r1.cmd('ifconfig r1-eth1 10.0.2.1 netmask 255.255.255.0 up')
    r1.cmd('ifconfig r1-eth2 10.0.3.1 netmask 255.255.255.0 up')
    r1.cmd('ifconfig r1-eth3 10.0.4.1 netmask 255.255.255.0 up')
    r1.cmd('ifconfig r1-eth4 10.0.5.1 netmask 255.255.255.0 up')
    r1.cmd('sysctl net.ipv4.ip_forward=1')

    h1 = configIntGW(net, 'h1', 'h1-eth0', '10.0.1.2', '10.0.1.1', '../user/dysco_daemon > dysco_daemon_h1.log &') 
    h2 = configIntGW(net, 'h2', 'h2-eth0', '10.0.2.2', '10.0.2.1', '../user/dysco_daemon > dysco_daemon_h2.log &') 
    h3 = configIntGW(net, 'h3', 'h3-eth0', '10.0.5.2', '10.0.5.1', '../user/dysco_daemon > dysco_deamon_h3.log &') 
    m1 = configInt(net, 'm1', 'm1-eth0', '10.0.3.2', '../user/dysco_daemon > dysco_daemon_m1.log &') 
    m1 = configInt(net, 'm1', 'm1-eth1', '10.0.4.2', '../user/tcp_proxy 5001 10.0.2.2 5002 %s  > tcp_proxy.log &' % splice_time) 


    time.sleep(1)
    
    m1.cmd('route add -net 10.0.1.0/24 gw 10.0.3.1')
    m1.cmd('route add -net 10.0.2.0/24 gw 10.0.4.1')
    m1.cmd('route add -net 10.0.5.0/24 gw 10.0.4.1')
    m1.popen('iperf -s -p 10000')
    h2.popen('iperf -s -p 5002')

    h1.cmd('../user/dysco_ctl policy 10.0.1.2 1 10.0.3.2 tcp port 5001') 
    m1.cmd('../user/dysco_ctl policy 10.0.4.2 1 10.0.2.2 tcp port 5002') 

    h1.cmd('ping 10.0.3.2 -c 2')
    h1.cmd('ping 10.0.2.2 -c 2 > /dev/null')
    m1.cmd('ping 10.0.2.2 -c 2 > /dev/null')
    
    #startTCPprobe("cwnd.txt")

    #h1.cmd("iperf -f m -c 10.0.3.2 -i 1 -t %s > iperf.log 2> iperf.err" % total_time)        
    CLI(net)
         
    #stopTCPprobe()

    call(["sysctl", "net.ipv4.tcp_congestion_control=cubic"])
    
    #call(["sysctl", "net.ipv4.tcp_sack=1"])
         
    Popen("killall dysco_daemon", shell=True).wait()
    Popen("killall tcp_proxy", shell=True).wait()
    Popen("killall iperf", shell=True).wait()

    time.sleep(1)
    
    net.stop()
    
    time.sleep(1)
    call(["rmmod", "tcp_probe"])
    call(["rmmod", "veth"])

def main(argv):
    splice_time = '25'
    total_time  = '60'
    try:
        opts, args = getopt.getopt(argv,"hs:t:",["splice_time=","total_time="])
    except getopt.GetoptError:
        print 'splice_veth_mininet.py -s <splice_time> -t <total_time>'
        sys.exit(2)
    for opt, arg in opts:
        if opt == '-h':
            print 'splice_veth_mininet.py -s <splice_time> -t <total_time>'
            sys.exit()
        elif opt in ("-s", "--splice_time"):
            splice_time = arg
        elif opt in ("-t", "--total_time"):
            total_time = arg
    print 'Splice time ', splice_time
    print 'Total time ', total_time
    run(splice_time, total_time)

if __name__ == "__main__":
    main(sys.argv[1:])
