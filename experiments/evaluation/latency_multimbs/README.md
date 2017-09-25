# Latency Distribution

This script gets latency in connection setup. The client establishes
1000 connections to the server via middleboxes. The script measures the
time of each $connect()$ call.

```sh
$ sudo ethtool -K eno2 tx off tso off gso off
$ sudo ethtool -K eno2 rx off gro off lro off
```

## Topology

```
```

## Run (K=20000)

### baseline


### dysco

```sh
```

## make the graph
```sh
 $ cd result
```
