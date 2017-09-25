package main

import (
        "github.com/google/gopacket"
        "github.com/google/gopacket/pcap"
        "log"
        "time"
        "os"
        "fmt"
        "net"
	"bytes"
)

var (
        snapshot_len    int32 = 1500
        promiscuous     bool = true
        err             error
        timeout         time.Duration = 0
        handle1         *pcap.Handle
        handle2         *pcap.Handle
        MLT             map[string]net.HardwareAddr
	ifmac1		net.HardwareAddr
	ifmac2		net.HardwareAddr
	rw_macda1	net.HardwareAddr
	rw_macda2	net.HardwareAddr
)

func forwardPacket(handleIn, handleOut *pcap.Handle) {
        packetSource := gopacket.NewPacketSource(handleIn, handleIn.LinkType())
        for {
                for pkt := range packetSource.Packets() {
		        data := pkt.Data()
		    	dstMAC := net.HardwareAddr(data[0:6])
	
	                if bytes.Compare(dstMAC, ifmac1) == 0 {
			        copy(data[0:6], rw_macda1)
			        copy(data[6:12], ifmac2)
                        } else if bytes.Compare(dstMAC, ifmac2) == 0 {
                                copy(data[0:6], rw_macda2)
    			        copy(data[6:12], ifmac1)
			}
	
                        handleOut.WritePacketData(data)
                }
        }
}

func main() {
        if len(os.Args) != 7 {
                fmt.Println("Usage: mb2 <if1> <if1_mac> <rw_macda1> <if2> <if2_mac> <rw_macda2>");
                os.Exit(1)
        }

        handle1, err = pcap.OpenLive(os.Args[1], snapshot_len, promiscuous, timeout)
        if err != nil {log.Fatal(err)}
        defer handle1.Close()
        
        handle2, err = pcap.OpenLive(os.Args[4], snapshot_len, promiscuous, timeout)
        if err != nil {log.Fatal(err)}
        defer handle2.Close()

        ifmac1, err = net.ParseMAC(os.Args[2])
        if err != nil {log.Fatal(err)}
        rw_macda1, err = net.ParseMAC(os.Args[3])
        if err != nil {log.Fatal(err)}
        ifmac2, err = net.ParseMAC(os.Args[5])
        if err != nil {log.Fatal(err)}
        rw_macda2, err = net.ParseMAC(os.Args[6])
        if err != nil {log.Fatal(err)}

        err = handle1.SetBPFFilter("ether dst " + os.Args[2])
        if err != nil {log.Fatal(err)}

        err = handle2.SetBPFFilter("ether dst " + os.Args[5])
        if err != nil {log.Fatal(err)}
        
        go forwardPacket(handle1, handle2)
        forwardPacket(handle2, handle1)
}
