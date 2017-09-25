#!/bin/sh

start()
{
    # flush and reset
    sudo conntrack -F
    sudo iptables -F
    sudo iptables -X
    sudo iptables -Z
    
    # default policy
    sudo iptables -P FORWARD DROP
    sudo iptables -A FORWARD -p icmp -j ACCEPT
    sudo iptables -A FORWARD -p udp -j ACCEPT
    sudo iptables -A FORWARD -p tcp --dport 22 -j ACCEPT
    sudo iptables -A FORWARD -p tcp --syn -m conntrack --ctstate NEW -j ACCEPT
    sudo iptables -A FORWARD -p tcp -m conntrack --ctstate ESTABLISHED -j ACCEPT
    sudo iptables -A FORWARD -m conntrack --ctstate INVALID -j DROP
}

stop()
{
    sudo iptables -P FORWARD ACCEPT
    sudo iptables -F

    sudo modprobe -r xt_conntrack
    sudo modprobe -r nf_conntrack_netlink
    sudo modprobe -r nf_conntrack_ipv4
    sudo modprobe -r nf_conntrack
}

show()
{
    sudo iptables -L -v
    sudo conntrack -L
}

usage()
{
    echo "Usage $0 [start|stop|restart]"
}

case "$1" in
    start)
        start
        ;;
    stop)
        stop
        ;;
    show)
        show
        ;;
    restart)
        stop
        start
        ;;
    *)
        usage
        exit 1
        ;;
esac
