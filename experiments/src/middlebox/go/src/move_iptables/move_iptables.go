/*
 * Imported from https://godoc.org/honnef.co/go/conntrack
 * and modified to move the states of conntrack.
 *
 */

package main

import (
        "bufio"
        "encoding/json"
        "fmt"
        "net"
        "os"
        "os/exec"
        "strconv"
        "strings"
	"github.com/pborman/getopt"
)

type Flow struct {
        Source      net.IP
        Destination net.IP
        SPort       uint16
        DPort       uint16
        Bytes       uint64
        Packets     uint64
        IcmpType    uint8
        IcmpCode    uint8
}

func (flow *Flow) Print() {
        fmt.Println("src      : ", flow.Source)
        fmt.Println("dst      : ", flow.Destination)
        fmt.Println("sport    : ", flow.SPort)
        fmt.Println("dport    : ", flow.DPort)
        fmt.Println("bytes    : ", flow.Bytes)
        fmt.Println("packets  : ", flow.Packets)
}

type Connection struct {
        Original  Flow
        Reply     Flow
        Protocol  string
        State     string
        Unreplied bool
        Assured   bool
        TTL       uint64
}

func (conn *Connection) Print() {
        conn.Original.Print()
        conn.Reply.Print()
        fmt.Println("Protocol : ", conn.Protocol)
        fmt.Println("State    : ", conn.State)
        fmt.Println("Unreplied: ", conn.Unreplied)
        fmt.Println("Assured  : ", conn.Assured)
        fmt.Println("TTL      : ", conn.TTL)
        fmt.Println()
}

func (conn *Connection) MakeConntrackCommand() string {
        cmd := "sudo conntrack -I"
        cmd += " -p "               + conn.Protocol
        cmd += " -s "               + conn.Original.Source.String()
        cmd += " -d "               + conn.Original.Destination.String()
        cmd += " -r "               + conn.Reply.Source.String()
        cmd += " -q "               + conn.Reply.Destination.String()
        if conn.Protocol == "tcp" {
                cmd += " --sport "          + strconv.FormatUint(uint64(conn.Original.SPort), 10)
                cmd += " --dport "          + strconv.FormatUint(uint64(conn.Original.DPort), 10)
                cmd += " --reply-port-src " + strconv.FormatUint(uint64(conn.Reply.SPort), 10)
                cmd += " --reply-port-dst " + strconv.FormatUint(uint64(conn.Reply.DPort), 10)
                cmd += " --state "          + conn.State
        }
        if conn.Protocol == "udp" {
                cmd += " --sport "          + strconv.FormatUint(uint64(conn.Original.SPort), 10)
                cmd += " --dport "          + strconv.FormatUint(uint64(conn.Original.DPort), 10)
                cmd += " --reply-port-src " + strconv.FormatUint(uint64(conn.Reply.SPort), 10)
                cmd += " --reply-port-dst " + strconv.FormatUint(uint64(conn.Reply.DPort), 10)
        }
        if conn.Protocol == "icmp" {
                cmd += " --icmp-type "      + strconv.FormatUint(uint64(conn.Original.IcmpType), 10)
                cmd += " --icmp-code "      + strconv.FormatUint(uint64(conn.Original.IcmpCode), 10)
        }
        cmd += " -t "               + strconv.FormatUint(uint64(conn.TTL), 10)

        return cmd
}

type Connections []Connection

type Message struct {
        Rules  string
        States Connections
}

func GetConfig() (string, error) {
        data, err := exec.Command("/bin/sh", "-c", "sudo iptables-save").Output()
        return string(data), err
}

func PutConfig(config string) (error) {
        _, err := exec.Command("/bin/sh", "-c", "sudo iptables-restore <<EOF\n" + config + "\nEOF").Output()
        if err != nil {
                fmt.Println(err)
        }
        return err
}

func GetState(options Options) (Connections, error) {
        opt := " "
        if *options.Proto != "" {
                opt += " --proto " + *options.Proto
        }
        if *options.Src != "" {
                opt += " -s " + *options.Src
        }
        if *options.Dst != "" {
                opt += " -d " + *options.Dst
        }
        if *options.Sport != 0 {
                opt += " --sport " + strconv.Itoa(*options.Sport)
        }
        if *options.Dport != 0 {
                opt += " --dport " + strconv.Itoa(*options.Dport)
        }
        data, err := exec.Command("/bin/sh", "-c", "sudo conntrack -L" + opt).Output()
        if err != nil {
                return nil, err
        }

        conn_states, err := parseState(data)
        if err != nil {
                fmt.Println("ERROR failed to get")
                os.Exit(1)
        }

        return conn_states, err
}

func PutState(conns Connections) (string, error) {
        var res string
        var err error
        for _, conn := range conns {
                cmd := conn.MakeConntrackCommand()
                fmt.Println(cmd)
                _, err = exec.Command("/bin/sh", "-c", cmd).Output()
                if err != nil {
                        fmt.Println(err)
                        res += "ERROR: " + cmd + "\n"
                }
        }
        return res, err
}

func parseState(data []byte) (Connections, error) {
        conns := make([]Connection, 0)
        for _, line := range strings.Split(string(data), "\n") {
                var (
                        proto, state       string
                        ttl                uint64
                        unreplied, assured bool
                        original, reply    map[string]string
                )
                original = make(map[string]string)
                reply = make(map[string]string)

                fields := strings.Fields(line)
                if len(fields) == 0 {
                        break
                }

                proto = fields[0]
                if proto == "tcp" {
                        state = fields[3]
                }

                ttl, _ = strconv.ParseUint(fields[2], 10, 64)

                for _, field := range fields[3:] {
                        if field == "[UNREPLIED]" {
                                unreplied = true
                        } else if field == "[ASSURED]" {
                                assured = true
                        } else {
                                kv := strings.Split(field, "=")
                                if len(kv) != 2 {
                                        continue
                                }
                                _, ok := original[kv[0]]
                                
                                var m map[string]string
                                if ok {
                                        m = reply
                                } else {
                                        m = original
                                }
                                m[kv[0]] = kv[1]
                        }
                }

                osport, _ := strconv.ParseUint(original["sport"], 10, 16)
                odport, _ := strconv.ParseUint(original["dport"], 10, 16)
                obytes, _ := strconv.ParseUint(original["bytes"], 10, 64)
                opackets, _ := strconv.ParseUint(original["packets"], 10, 64)

                rsport, _ := strconv.ParseUint(reply["sport"], 10, 16)
                rdport, _ := strconv.ParseUint(reply["dport"], 10, 16)
                rbytes, _ := strconv.ParseUint(reply["bytes"], 10, 64)
                rpackets, _ := strconv.ParseUint(reply["packets"], 10, 64)

                otype, _ := strconv.ParseUint(original["type"], 10, 8)
                ocode, _ := strconv.ParseUint(original["code"], 10, 8)

                conn := Connection {
                        Original: Flow {
                                Source: net.ParseIP(original["src"]),
                                Destination: net.ParseIP(original["dst"]),
                                SPort: uint16(osport),
                                DPort: uint16(odport),
                                Bytes: obytes,
                                Packets: opackets,
                                IcmpType: uint8(otype),
                                IcmpCode: uint8(ocode),
                        },
                        Reply: Flow {
                                Source: net.ParseIP(reply["src"]),
                                Destination: net.ParseIP(reply["dst"]),
                                SPort: uint16(rsport),
                                DPort: uint16(rdport),
                                Bytes: rbytes,
                                Packets: rpackets,
                        },
                        Protocol:  proto,
                        State:     state,
                        Unreplied: unreplied,
                        Assured:   assured,
                        TTL:       ttl,
                }

                if conn.State == "" {
                        if conn.Unreplied {
                                conn.State = "UNREPLIED"
                        } else if conn.Assured {
                                conn.State = "ASSURED"
                        }
                }
                
                conns = append(conns, conn)
        }

        return conns, nil
}

func Client(ip string, port int, options Options) {
        // get
        rules, _ := GetConfig()
        states, _ := GetState(options)
        if states == nil {
                fmt.Println("NOTICE: no states")
        }
        
        msg := Message {
                Rules: rules,
                States: states,
        }
        smsg, err := json.Marshal(msg)
        if err != nil {
                fmt.Println(err)
                return
        }

        conn, err := net.Dial("tcp", ip + ":" + strconv.Itoa(port))
        if err != nil {
                fmt.Println("ERROR: failed to connect server")
                return
        }
        fmt.Fprintf(conn, string(smsg) + "\n")
        response, _ := bufio.NewReader(conn).ReadString('\n')
        fmt.Print(response)
}

func Server(port int) {
        fmt.Println("start server...")
        ln, _ := net.Listen("tcp", ":" + strconv.Itoa(port))
        sock, _ := ln.Accept()
        defer sock.Close()

        smsg, _ := bufio.NewReader(sock).ReadString('\n')

        var msg Message
        err := json.Unmarshal([]byte(smsg), &msg)
        if err != nil {
                sock.Write([]byte("ERROR: failed to unmarshal\n"))
                return
        }

        err = PutConfig(msg.Rules)
        if err != nil {
                fmt.Println(err)
                sock.Write([]byte("ERROR: failed to iptables-restore\n"))
                return
        }

        res, err := PutState(msg.States)
        if err != nil {
                sock.Write([]byte(res + "\n"))
                return
        }
        sock.Write([]byte("OK" + "\n"))
}

type Options struct {
        Src *string
        Dst *string
        Sport *int
        Dport *int
        Proto *string
}

func main() {
	help   := getopt.BoolLong("help", 0, "help")
	ipaddr := getopt.StringLong("client", 'c', "127.0.0.1", "run in client mode, connecting to <server>")
	port   := getopt.IntLong("port", 'p', 8081, "server port number")
	server := getopt.BoolLong("server", 's', "run in server mode")

	src    := getopt.StringLong("src",   0, "", "filter: source ip address")
	dst    := getopt.StringLong("dst",   0, "", "filter: destination ip address")
	sport  := getopt.IntLong("sport",    0, 0,  "filter: source port number")
	dport  := getopt.IntLong("dport",    0, 0,  "filter: destination port number")
	proto  := getopt.StringLong("proto", 0, "", "filter: tcp|udp|icmp")
	
	getopt.Parse()
	if *help {
		getopt.Usage()
		os.Exit(0)
	}

        options := Options {
                Src: src,
                Dst: dst,
                Sport: sport,
                Dport: dport,
                Proto: proto,
        }

        if *server {
                Server(*port)
        } else if *ipaddr != "" {
                Client(*ipaddr, *port, options)
        } else {
		getopt.Usage()
		os.Exit(0)
	}
}
