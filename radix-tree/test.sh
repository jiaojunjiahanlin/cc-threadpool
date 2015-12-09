fio -filename=/mnt/dmcache/2G.file -direct=1 -iodepth 1 -thread -rw=read -ioengine=psync -bs=512k -size=2G -numjobs=10 -runtime=1000 -group_reporting -name=mytest
