# 说明
本代码简答地处理了ICMP的ECHO包，并回应以ECHO REPLY。

## 首先运行这个程序:
```
gcc tun.c
./a.out
TUN name is tun0 
```

### 然后输入
```
ifconfig tun0 0.0.0.0 up
route add 10.10.10.1 dev tun0
ping 10.10.10.1
```

### 输出结果
```
PING 10.10.10.1 (10.10.10.1) 56(84) bytes of data.
64 bytes from 10.10.10.1: icmp_seq=1 ttl=64 time=1.09 ms
64 bytes from 10.10.10.1: icmp_seq=2 ttl=64 time=5.18 ms
64 bytes from 10.10.10.1: icmp_seq=3 ttl=64 time=3.37 ms
```
