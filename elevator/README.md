# 电梯模拟操作说明

1. 首先，在编译的时候需要开启 `USE_LOCAL_SIMULATE_DATA` 功能：

   ```bash
   cmake -B build -DUSE_LOCAL_SIMULATE_DATA=ON
   cmake --build build
   ```
2. 准备抓取的模拟数据

    这里可以使用 `examples/0000000000000000--1------25--2负一到25然后到负2用于tui模拟.csv`
    这个文件对应于 `T02` 货梯，从 -1 逐层到 25 楼，然后直接到 -2 的过程。
    
  将模拟数据文件改名为 `simulate.csv` 放到当前运行目录下。

3. 运行

   ```bash
   cd build
   ./elevator/elevatord
   ```

   同时会生成以下文件：
   - hrlog-YYYY-mm-dd-H-M-S.log： 本次运行的日志
   - result.csv 在开启 `dump` 功能时，会将滤波后数据保存在这个文件中
 
 4. 楼层修正
 
    这个数据对应是从 -1 （-6.1M）开始运行的，可以在 `core.c` 中赋一个初始值：
    
    ```c
        _accelerometer_motion.height = 6.1;
    ```
