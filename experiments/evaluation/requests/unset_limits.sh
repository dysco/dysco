sudo sh -c 'sysctl -w fs.file-max=6568928'
sudo sh -c 'sysctl -w fs.nr_open=1048576'
sudo sh -c 'sysctl -w net.ipv4.ip_local_port_range="32768 60999"'
sudo sh -c 'sysctl -w net.ipv4.tcp_mem="770364	1027155	1540728"'
