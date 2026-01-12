# elinkd

> 孟祥朋 2025-07-10

## 介绍

这是一个非常简单的物联网框架，主要包含两个部分：

-  `uloop` 主线程事件循环, 我们的 `ubus` 就运行在这个主线程中。
   
-  `mosquitto` 子线程,  `MQTT` 消息等回调运行在该线程。

由于 `ubus` 是不支持多线程的，所以在调用的时候务必谨慎，尤其是当你接收到 `MQTT` 的消息的时候，是不能直接在回调函数中直接调用 `ubus` 接口的。

为了解决这个问题，我们添加了两个函数 `post_timer_task/post_async_task` 分别用于将任务发送给主线程来执行。

> 这个方案与我们之前的 `uviot` 不同， `mosquitto` 是运行在独立的线程中，采用的是 `mosquitto` 自身的 `loop` 事件机制。而 `uviot` 我们没有使用 `mosquitto` 自身事件机制，采用的是 `libuv main loop` 所以，各种调用与 `mosquitto` 调用是在同一线程中的
## 主题订阅与发布

本模块主要是通过 `MQTT` 与物联网平台进行通信完成电梯摄像头相关功能的管控。

所以，我们首先要做的就是订阅以及发布主题。

目前系统中已经预置了阿里云规范的 `property/service` 两个主题：

- `/sys/xxxx/yyyy/thing/event/property`
- `/sys/xxxx/yyyy/thing/service`

如有其他主题需求，可以参考 `topic_property.c/topic_service.c`.

具体是要定义：

```c
struct topic {
    char name[128];
    char topic[256];
    int period;
    int auto_publish; // auto publish when connected
    enum topic_type {
        TOPIC_TYPE_PUBLISH = 0,
        TOPIC_TYPE_SUBSCRIBE
    } type;
    // on start
    // on stop
    union {
        // called with message on subscribed topic
        int (*on_message)(void *payload, int len);
        // called before publish topic
        int (*on_publish)(void **payload, int *len);
    } callback;
};
```

相关字段含义如下：

- `name`: 主题简短名字
- `topic`: 主题完整路径
- `period`: 毫秒，仅用于发布类型主题，非 0 表示该主题将间歇性上报
- `auto_publish`: 1/0 连接成功后，是否自动发布，仅用于发布类型主题
- `type`: 主题类型，发布或者订阅
- `callback`: 不同类型主题回调
   - `on_publish` 需要给 `payload` 分配内存，系统会自动释放

之后，通过 `iot_topic_register` 来注册该主题，在收到消息的时候会自动触发相关 `on_message` 回调函数。对于 `PUBLISH` 主题，如果设置了周期发送，那么会周期性自动调用 `on_publish` 函数，如果需要主动触发数据发送，可以通过 `iot_topic_publish_async` 来触发。

## 属性

属性相关主题是在 `topic_property.c` 中处理的，在收到相关消息的时候，会遍历 `property.c` 中 `properties_tbl` 数组访问属性值。

默认只有`property.dirty == 1` 的属性才会在下次 `on_publish` 回调中上传。

那么如何新增属性字段呢?

首先，我们要了解下属性相关结构体 `struct property`:

```c
enum property_type {
    E_UNKNOWN = 0,
    E_NUMBER, // use int64
    E_DECIMAL,
    E_STRING,
    E_BOOLEAN,
};

struct property_value {
    enum property_type type;
    union {
        int64_t number;
        double decimal;
        const char* string;
        bool boolean;
    } val;
    int preallocated;  // union value is preallocated, you no need free it...
};

struct property {
    const char* name;
    enum property_type type;
    // some property no need get
    // dispatch when property is changed
    // there update value directly and mark dirty
    int (*getter)(struct property*self);
    int (*setter)(struct property*self, struct property_value *value);
    struct property_value value;
    int dirty;
};
```

然后按下面方法来新增一条属性：

1.  在 `properties_tbl` 中新加一条 `struct property` 成员。
2.  明确字段名称
3.  明确字段值类型：E_STRING/E_NUMBER/E_DECIMAL/...
4.  是否常量值字段？常量值字段可以省略 `getter/setter` 处理函数，直接在 `struct property.value` 中赋值即可
5.  根据需要定义 `getter/setter` 函数
    - 在 `getter` 的调用中，我们没有额外传递 `struct property_value`, 请更新 `property.value` 字段即可
    - 在 `setter` 的调用中，我们额外传递 `struct property_value`, 实现时根据自身逻辑来更新 `property.value` 的值

## 方法

目前方法的实现在 `service.c` 中， 参考 `svc_action_tbl` 添加自己的方法即可。

目前仅实现必要方法，一些调试的方案，因为涉及到漏洞风险，是没有添加的。

如有必要自行放开相关注释。

## 事件

事件大家可能不会用到，目前在 `topic_event.c` 中实现，仅仅支持楼层标定的事件。

如果后续有其他事件也可以参考自行添加。

## ubus 跨进程条用

本模块会将管理平台下发的指令分发到不同的模块中，可能会存在很多的跨进程调用问题。

为了安全，我们将这部分功能从 `elevatord` 中独立出来，目前与电梯相关的参数是通过 `ubus` 远程调用 `elevatord`。

在使用 `ubus` 接口的时候，请务必知晓 `ubus` 不允许在非主线程中调用，例如不能直接在 `topic` 回调中调用。

## 注意事项

1.  `ubus` 接口不支持多线程调用，非主线程不允许调用；
2.  主线程不允许执行耗时任务；


## 变更记录

### 2026-01-12

1. 上报楼层号 `floor` 、门状态 `door_status` 、人数 `passenger_count` 等属性（无需调用 get 方法，采用主动变更上报方式）
2. 上报故障信息（简化）`event/FaultEvent/post`

    Topic: /sys/G9k7UzhGu8AZtlOf/LC202508B001000003/thing/event/AutoFloorCalibrationEvent/post

    ```json
    {
      "id": "16",
      "version": "1.0.0",
      "fault": {
        "type": "kunren",
        "status": 1
      }
    }
    ```
