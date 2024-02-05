# OS Notes



## 实验

### Lab 0

​	lab0的实验首先需要安装docker，但是docker的安装坑很多，下面浅浅记录一下：

**Docker installation *Kengs***

​	docker作为一个成熟的产品，它提供了desktop、engine这两种版本，显然，一个是有图形化界面，一个没有，而desktop版本又分别可以安装在mac、win和linux上，因此选择是困难的，我们有以下这些选择：

- docker desktop inMac：没有使用过，不做评价；

- docker desktop in Windows：有很大的前置要求，比如hyper-v、kvm等，这些都是windows专业版系统上才会安装的东西，因此很可能会无法运行；

  > 具体前置要求请看[docker的官方安装网站](https://docs.docker.com/desktop/)
  >
  > 以及这篇文章——[win10在WSL2上安装docker的两种方法](zhihu.comhttps://zhuanlan.zhihu.com/p/148511634)

- docker desktop in Linux：

  - 如果是在虚拟机中安装docker desktop for linux，那应该不会出现任何问题；

    但是这样有几个缺点令人难以接受：
  
  1. 虚拟机需要占据巨大的磁盘空间，运行时要占据大量的内存运算，开销太大；
  2. docker本身是为了替代虚拟机而出现的技术，现在为了装docker先装个虚拟机不合情理；
  3. 对于我本人（由于我水平很菜），虚拟机在实现宿主机和容器数据卷的映射、宿主机和虚拟机的远程(SSH)连接问题、共享文件夹等问题上有些过于繁琐，主观上不愿意再使用；
  
  - 如果是在WSL2的Ubuntu中安装docker desktop for linux，还是会遇到前面的问题，因为硬件问题没有解决：

```可能出现的报错信息
  Unit docker.service not found.
  Unit docker-desktop.service can not be found.
  ...
  lsmod | grep kvm <!-可能会没反应的一条命令--->
  ...
```
​      ***同时：*** 不同于完全linux虚拟机方式，WLS2下通过`apt install docker-ce`命令安装的docker无法启动，因为WSL2方式ubuntu里面没有systemd。上述官方get-docker.sh安装的docker，dockerd进程是用ubuntu传统的init方式而非systemd启动的；

- docker engine：

  Docker Engine只能装在Linux系统上，因此也有下面两种选择：

  - 在WSL2上

  ​    我的经验是可以运行docker，但是也有一些问题：无法完成volume映射、docker守护程序位置未知、docker默认端口未知...

  - 在虚拟机上

  ​    同上面理由一样，开销太大...

----

  好吧，最后还是选择了安装docker desktop for windows，我有以下几点原因：

1. 周末淘宝上买了个win10专业版，好像可以装Hpyer-V...

2. 一开始Docker Desktop in Windows在抽风，一直在starting the docker engine加载，最后通过取消注册了因为docker-desktop而出现的两个wsl分发版`docker-desktop`和`docker-desktop-data`，然后重启后再打开desktop，竟然神奇的好了，有点玄学，不建议学习...

---

补充几个资料网站：

[Docker操作之创建容器](https://blog.csdn.net/gtdisscary/article/details/120463699)

[从零开始的Docker Desktop使用（￣︶￣）](https://blog.csdn.net/qq_39611230/article/details/108641842) 



## 课程和作业

