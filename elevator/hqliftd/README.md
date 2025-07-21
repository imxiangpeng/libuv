# 配置文件参数

我们提供了遍历的配置文件参数获取方法，参数的获取可能零星的分布在多个文件中，这里我们统一记录一下：

1.  HQLIFTD_CONFIG_PATH: /etc/hqliftd/hqliftd.conf

    - `MQ_SERVER=mq.hqszjs.com`
    - `MQ_PORT=1883`
    - `MQ_KEEPALIVE=60`
    - `MQ_USERNAME=inspur`
    - `MQ_PASSWORD=inspur88*`
    - `REALTIME_REPORT_PERIOD_MS=1000`
    - `SPEED_LIMIT_THREHOLD=2.8`
    - `FTP_ADDRESS=`
    - `FTP_USERNAME=`
    - `FTP_PASSWORD=`
    - `LIFTFAULT_REPORT_SWITCH=`
    - `LIFTFAULT_DOOR_DETECT_TIMEOUT=` 电梯停靠后多久不开门认为困人（有人），单位毫秒, 因为目前开关门算法存在相当概率识别非常门或者不准确的情况，所以目前设置的非常大，最开始我们设置的是 5s
       放到 `elevatord.conf` 中作为 `eguard` 参数。
    - `LIFTFAULT_PERSON_LONG_INSIDE_TIMEOUT` 电梯关门并且静止情况下，在电梯内有人时，多久认为困人，单位毫秒, 默认设置 60s
       放到 `elevatord.conf` 中作为 `eguard` 参数。
    - `LIFTFAULT_REPORT_LIMIT_PER_DAY` 限制每类故障每天上报次数，排除困人和电瓶车检测，仅限制 （反复开关门、关门异常、开门行梯）
    - `RUNINFO_REPORT_SWITCH` 运行数据上报开关
    - `LIFTFAULT_VIDEO_UPLOAD_SWITCH` 故障视频上报开关，备用
       困人视频在上传的时候因为要将多个视频进行合成，而我们系统经常误判困人事件，有时出现十几个小时的困人，例如 `GD500112000062` 在 20250716 的时候产生了 4G 多的视频 ftp://inspur@ftp.hqszjs.com:2100/event_files/GD500112000062/20250716_075937.mp4
