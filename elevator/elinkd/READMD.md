# elinkd

> 孟祥朋 2025-07-10

## 介绍

这是一个非常简单的物联网框架，主要包含两个部分：

-  `uloop` 主线程事件循环, 我们的 `ubus` 就运行在这个主线程中。
   
-  `mosquitto` 子线程,  `MQTT` 消息等回调运行在该线程。


由于 `ubus` 是不支持多线程的，所以在调用的时候务必谨慎，尤其是当你接收到 `MQTT` 的消息的时候，是不能直接在回调函数中直接调用 `ubus` 接口的。

为了解决这个问题，我们添加了两个函数 `post_timer_task/post_async_task` 分别用于将任务发送给主线程来执行。

## 主题订阅与发布

本模块主要是通过 `MQTT` 与物联网平台进行通信完成电梯摄像头相关功能的管控。

所以，我们首先要做的就是订阅以及发布主题。

目前系统中已经预置了阿里云规范的 `property/service` 两个主题：

- `/sys/xxxx/yyyy/thing/event/property`
- `/sys/xxxx/yyyy/thing/service`

如有其他主题需求，可以参考 `topic_property.c/topic_service.c`.


## ubus 跨进程条用

本模块会将管理平台下发的指令分发到不同的模块中，可能会存在很多的跨进程调用问题。

为了安全，我们将这部分功能从 `elevatord` 中独立出来，目前与电梯相关的参数是通过 `ubus` 远程调用 `elevatord`。

在使用 `ubus` 接口的时候，请务必知晓 `ubus` 不允许在非主线程中调用，例如不能直接在 `topic` 回调中调用。
