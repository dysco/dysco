sudo sh -c 'sysctl -w fs.file-max=11000000'
sudo sh -c 'sysctl -w fs.nr_open=11000000'
sudo sh -c 'ulimit -n 11000000'
sudo sh -c 'sysctl -w net.ipv4.ip_local_port_range="1025 65535"'
sudo sh -c 'sysctl -w net.ipv4.tcp_mem="100000000 100000000 100000000"'

