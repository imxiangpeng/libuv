# 配置文件参数

我们提供了遍历的配置文件参数获取方法，参数的获取可能零星的分布在多个文件中，这里我们统一记录一下：

1.  HQLIFTD_CONFIG_PATH: /etc/hqliftd/hqliftd.conf

    - `MQ_SERVER=mq.hqszjs.com`
    - `MQ_PORT=1883`
    - `MQ_KEEPALIVE=60`
    - `MQ_USERNAME=inspur`
    - `MQ_PASSWORD=inspur88*`
    - `REALTIME_REPORT_PERIOD_MS=1000`
    - `SPEED_LIMIT_THREHOLD=3.1`
    - `FTP_ADDRESS=`
    - `FTP_USERNAME=`
    - `FTP_PASSWORD=`
    - `LIFTFAULT_REPORT_SWITCH=`
    - `LIFTFAULT_DOOR_DETECT_TIMEOUT=` 电梯停靠后多久不开门认为困人（有人），单位毫秒, 因为目前开关门算法存在相当概率识别非常门或者不准确的情况，所以目前设置的非常大，最开始我们设置的是 5s
    - `LIFTFAULT_PERSON_LONG_INSIDE_TIMEOUT` 电梯关门并且静止情况下，在电梯内有人时，多久认为困人，单位毫秒, 默认设置 60s
