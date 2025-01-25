Your RIP application (router process) should be run with a command line argument, which is a config file (details of the config file is discussed later in this document). You should run your RIP applications in separate shells. Router failure will be simulated by killing a router process. The syntax to run the RIP application:
RIP <config_file>

Output:
Your program should print its route vectors (i.e. routing tables) as soon as it gets/sends one from/to any of its neighbors. Feel free to show more debugging info on the console.

Compulsory Features:
- Implement Periodic, Expiration, and Garbage Collection Timers

Optional Features (Bonus Marks):
- Exchange packets between routers in RIPv1 packet format, so that it can be analyzed by Wireshark

Config File Format:
This is a three column text file meant to describe the  connectivity information among neighbors. Each line describes the IP address of this router and that of one of its neighbor's. The first column represents the interface ID, the second column of each line is the IP address of an interface of this router and the third column represents the IP address of a directly connected neighbor. A sample config file:
1  192.168.1.1   192.168.1.2
2  192.168.2.1.  192.168.2.254
3  192.168.9.2.  192.168.9.1
From the above mentioned sample config file, it can be assumed that there are three interfaces of this router and the IP addresses of those are 192.168.1.1, 192.168.2.1, 192.168.9.2. The third column represents the IP address of a neighboring router. So the first line of the config file says that there is a link between interface 1 (IP address 192.168.1.1) and an interface of a neighboring router whose IP address is 192.168.1.2. And so on..
