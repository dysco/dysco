# Latency Distribution

This script gets latency in connection setup. The $client$ first
establishes $K$ connections as 'existing' sessions, then establishes
1000 connections to the server via middlebox. The script measures the
time of each $connect()$ call.


## Topology

```
  clients          middlebox          servers
 ---------------------------------------------
   dx002     LAN1             LAN2     dx003
   dx004  ---------- dx001 ----------  dx005
   dx006                               dx007
   dx008                               dx009
```

## Run (K=20000)

### baseline

```sh
 [dx005] $ ./run_baseline_server2_initconn.sh 10000
 [dx007] $ ./run_baseline_server3_initconn.sh 10000
 [dx004] $ ./run_baseline_client2_initconn.sh 10000
 [dx006] $ ./run_baseline_client3_initconn.sh 10000 

 [dx003] $ ./run_baseline_server.sh
 [dx002] $ ./run_baseline_client.sh > result/result_K20000_baseline_1.txt
```

### dysco

```sh
 [dx005] $ ./run_dysco_server2_initconn.sh 10000
 [dx007] $ ./run_dysco_server3_initconn.sh 10000
 [dx004] $ ./run_dysco_client2_initconn.sh 10000
 [dx006] $ ./run_dysco_client3_initconn.sh 10000 

 [dx001] $ ./run_dysco_middlebox.sh
 [dx003] $ ./run_dysco_server.sh
 [dx002] $ ./run_dysco_client.sh > result/result_K20000_dysco_1.txt
```

## make the graph
```sh
 // change the file names in the script 'make_graph.sh'
 $ cd result
 $ ./make_graph 20000 pdf
```
