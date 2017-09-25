# how to run the evaluation

1. ./host_checker.sh
   
   You can check whether all hosts are available to use for this evaluation.


2. ./autorun.sh

    You can specify:
    - the number of connections: CONN
    - which clients/servers/proxies to use (run_xxx)


3. ./get_results.sh
    
    You can get all results from clients and proxies.


# how to make the graph

1. cd results

2. ./make_graph_throughput.sh

3. edit make_graph_cpu.sh
   You have to change the value "+22" in the line that makes cpu_proxy_1.txt.tmp to fit the first value to the first line of cpu_proxy_1.tmp. Also do the same procedure for cpu_proxy_[2-4].txt.

4. ./make_graph_cpu.sh

5. edit make_graph_multiplot.sh as same as 3.

6. ./make_graph_multiplot.sh
