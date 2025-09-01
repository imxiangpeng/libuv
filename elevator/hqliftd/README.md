# 配置文件参数

我们提供了遍历的配置文件参数获取方法，参数的获取可能零星的分布在多个文件中，这里我们统一记录一下：

1.  HQLIFTD_CONFIG_PATH: /etc/hqliftd/hqliftd.conf

    - `MQ_ID=device_serialno` MQTT client id, 默认采用设备序列号
    - `MQ_PROTO=mqtt` # 支持 `mqtt/mqtts`, `mqtts` 会使用 `ssl/tls` 来连接，开启 `ssl` 会对服务器进行证书校验，如果服务器采用的是自签名证书需要特别注意。
    - `MQ_SERVER=mq.hqszjs.com`
    - `MQ_PORT=1883`
    - `MQ_KEEPALIVE=60`
    - `MQ_USERNAME=inspur`
    - `MQ_PASSWORD=inspur88*`
    - `MQ_TOPIC_SUB_COMMAND` 订阅命令主题 `/API/V1/Down/LC4005CB4E215D2A2/Command`
    - `MQ_TOPIC_PUB_COMMAND_RESPONSE` 命令响应主题 `/API/V1/Down/LC4005CB4E215D2A2/Command/Response`
    - `MQ_TOPIC_PUB_HEARTBEAT` 心跳主题 `/API/V1/Up/HeartBeat`
    - `MQ_TOPIC_PUB_LIFTSTATE` 运行状态主题 `/API/V1/Up/LiftState`
    - `MQ_TOPIC_PUB_LIFTFAULT` 故障上报主题 `/API/V1/Up/LiftFault`
    - `MQ_TOPIC_PUB_LIFTRUNINFO` 运行记录上报主题 `/API/V1/Up/LiftRunInfo`
    - `REALTIME_REPORT_PERIOD_MS=1000`
    - `SPEED_LIMIT_THREHOLD=2.8`
    - `LIVE_URL=rtmp://srs.hqszjs.com:1935/live/device_serialno`
    - `LIVE_TIMEOUT` 直播命令有效时长（秒）60
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
    - `RESCURE_MODE` 救援模式，默认手动模式 2， 手动模式下困人事件不会主动上报，仅在用户按下救援按钮情况下才会上报。自动模式 1，检测到困人事件后就会立马上报。另外所有事件（困人与其他）都会有一个超时机制，操作 2 小时 30 分没有结束的，自动上报结束事件。
      注意，目前没有独立的设置，是从困人设置模式中电话拨打方式那里同步过来的。
    

# 救援按键

厚齐要求在救援模式设置为手动的时候，不要主动上报故障事件以及视频等

所以，我们就将之前读取基层的方式拿过来读取救援按键 GPIO， 但是发现无法读取。

这个是由于 libgpiod 采用边缘触发检测的时候，会独占 GPIO 资源。


没办法我们采用简单的轮询的方式来识别，但是这样的话，我们是一直查询还是在判定困人后再检测几秒呢？

检测到困人之后再取识别按钮状态吧。


# 厚齐平台不支持 Qos2

一直频繁连接，然后断开。


	
