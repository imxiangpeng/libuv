# 阿里云平台

1.  我们时候预注册方式，动态注册虽然在生产的时候可以少写一个字段，但是及其的不安全，尤其每个设备都是需要单独计费的情况下；
1.  目前我们使用 MAC 地址（去除 ':' 后作为设备名字）；目前使用 MAC的原因是因为我不知大序列号规范什么样的；
1.  ClientId 我们采用产品公钥与设备名字通过 `.` 连接的方式来拼接；


# 配置文件参数

我们提供了遍历的配置文件参数获取方法，参数的获取可能零星的分布在多个文件中，这里我们统一记录一下：

1.  ELEVATORD_CONFIG_PATH: /etc/elevatord/elevatord.conf

    elevatord 运行参数

    - `ACCELEROMETER_SAMPLING_RATE_HZ`： 加速度采样参数，默认 100Hz 或者 200Hz（double 类型，但是请使用整数）
    - `BAROMETER_SAMPLING_RATE_HZ`： 气压传感器采样参数，系统默认 （50Hz）, 类型 double， 因为可以设置 `12.5Hz`
    - `BAROMETER_PREDICT_STATIONARY_SLOPE`: 气压判定静止斜率(0.1)
    - `BAROMETER_PREDICT_STATIONARY_STDDEV`: 气压判定静止标准差(1.5, 我们测试一般在 1 以下，但是个别会到 1 冒头，但是运行时基本都在 2 以上，可以在 dump 数据中分析)
    
    - `IOT_REPORT_SWITCH`： 阿里云平台参数上报开关（必要的时候，可以关闭运行参数的上报， 但是启动时相关参数还是正常上报的） 
    - `EGUARD_ALARM_SWITCH`: 0/1 配置 eguard 是否播放音频通知
    - `EGUARD_DTOF_SWITCH`: 0/1 配置是否开启遮挡检测
    - `EGUARD_DTOF_OCCLUSION_DISTANCE`: 配置 DTOF 遮挡检测距离，单位毫米
    - `EGUARD_DOOR_ZONE_STOPPED_THRESHOLD`: 非门区停车检测距离，注意，因为加速度和气压都可能有误差，我们仅在两者同时判定超过这个阈值的时候才上报
    - `EGUARD_DOOR_CONTROL_ENABLED`: 是否通过继电器控制电梯门，例如检测到电瓶车后，是否禁止关门

2.  SENSOR_CALIBRATION_CONF： /etc/elevatord/sensor_calibration.conf

    传感器校准参数，不能删除。

    -  `CALIBRATED`: 1/0 是否已经完成校准
    -  `G_1000`： 校准后，计算的本地重力加速度的值 * 1000
    -  `BIAS_ACCEL_X_1000`: 加速度 X 零偏值 * 1000
    -  `BIAS_ACCEL_Y_1000`: 加速度 Y 零偏值 * 1000
    -  `BIAS_ACCEL_Z_1000`: 加速度 Z 零偏值 * 1000
    -  `PITCH_1000`: 安装俯仰角 * 1000
    -  `ROLL_1000`: 安装翻滚角 * 1000

# 气压更新策略

1.  每次到达基层强制更新内存数据(还是修改为每次停靠都更新)
2.  电梯静止 60s 情况下，当变化超过 10Pa 的时候，自动更新写入文件
3.  上面策略是不稳定的，但是我又不能频繁写入；（要不要考虑 nand 寿命）

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
