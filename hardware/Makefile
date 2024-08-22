all: mem_bw cpu_acc numa

mem_bw: mem_bw_test.c
	gcc -o mem_bw mem_bw_test.c -pthread -mavx

cpu_acc: cpu_acc_test.c
	gcc -o cpu_acc cpu_acc_test.c -pthread

numa: numa_test.c
	gcc -o numa numa_test.c -pthread -mavx -lnuma

clean:
	rm -rf mem_bw cpu_acc numa