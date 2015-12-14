fio -filename=/mnt/dmcache/2G.file -direct=1 -iodepth 1 -thread -rw=randread -ioengine=psync -bs=512k -size=200M -numjobs=2 -runtime=1000 -group_reporting -name=mytest
