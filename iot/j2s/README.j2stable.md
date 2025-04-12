# j2stable

> 孟祥朋 20240222

`j2sobject` 的实现满足了我们很多场景下的数据交互，但是随着业务的发展，我们需要通过 `JSON` 存储更多的数据条目，也就是我们需要的是一组数据，而不是一条数据。
这个概念有点像数据库的概念，更具体点是像数据库中表的概念；表中存储是一条条的数据记录，每条数据由不同的字段表示。

这里一条记录就对应我们上面提到的 `j2sobject` 对象，如何维护一组记录呢？

答案很自然的就是 `[{},{}]` 的方式来存储。

所以，我们对 `j2sobject` 进行了扩展，使其可以支持这种场景，并且抽象了 `j2stable` 数据类型。


## 设计理念

设计一张表（JSON 文件），其中以数组形式存放一条条数据记录，支持对表中数据的“查询”、删除、“插入”等操作。

当然我们不需要支持 `SQL` 语法。

适合的应用场景：数据量不大、更新不频繁，适合使用 `JSON` 格式存储数据的情况。

所有的数据将以 `JSON` 数组的形式存储在特定文件（文件名，我们定义为表名）中，不需要支持 `SQL`, 但是需要实现以下功能：

1.  查询：获取所有数据，不支持条件查询
2.  删除：删除指定数据，不支持条件删除
3.  更新：更新已有数据

为了考虑兼容性，可能需要存储额外的数据，所以 `JSON` 文件格式也可能是：

```json
// 1. using raw json
[{}]

// 2. support metadata
{"version":1,"data":[{}]}
```

可以看到，我们设计是非常简单的，正常数据库很多会采用红黑树等形式来加速查找以及插入性能，我们面向的就是简单应用场景，如果你需要复杂逻辑那么本方案是不适合的，可以考虑 sqlite 等。


## 设计原理

在实现上，我们借助前面提到的 `j2sobject` 来表示一条记录, 考虑到我们可能需要维护一个自增的 `id`, 所以，我们需要实现一个 `j2sobject` 对象：

```c
struct j2stbl_object {
  J2SOBJECT_DECLARE_OBJECT;
   // auto increase id, readonly, do not modify it!
  int __id__;
};
```

这里我们将字段命名为 `__id__`;

用户需要保存其他额外数据，就需要实现自己的 `j2sobject` 对象，因为我们想在内部维护 `__id__` 字段。

在使用时, 需要用户基于 `strcut j2stbl_object` 来定义自己的数据结构体，这里我们提供了简易的宏 `J2STBL_DECLARE_OBJECT` 来简化工作。

```c
typedef struct {
    J2STBL_DECLARE_OBJECT;
    int id;
    char mac[18];
    int type;
} j2stbl_mactbl_t;
```

我们查询到的数据可能是多条数据，多条数据之前是通过双链表的形式来关联的，在查询时我们将返回数据/数组头节点，可以借助 `struct j2sobject` 对象中 `next/prev` 字段遍历所有数据。
(我们没有设计那么复杂，正常数据库很多会采用红黑树等形式来加速查找以及插入性能，我们面向的就是简单场景，如果你需要复杂逻辑那么本方案是不适合的，可以考虑 sqlite 等)

`query` 操作就是直接读取 `JSON` 文件，通过 `j2sobject` 接口，返回数据对象（头节点）；
`insert` 实现原理是将 `j2sobject` 添加到末尾，并且依据上一个元素 `id` 实现递增行为，然后保存到文件中。
`update` 用于直接操作 `j2sobject` 子结构体，并且完成数据的更新，然后直接更新到文件中。
`delete` 直接在原有双链表上解除需要删除的对象，然后更新到文件中。

上面这些的实现都是依赖 `j2sobject` 提供的双链表机制，虽然我们提供了 `api`, 其实用于也可以直接调用 `j2sobject` 接口完成相关操作。


## API

我们用到了两个数据结构：

1.  struct j2stable: 代表一张表

    ```c
    struct j2stable {
      char *path;

      int state;

      struct j2sobject* priv;
    };

    ```
2.  struct j2stbl_object: 代表一条记录，用于需要扩展该结构体以添加更多字段

    ```c
    struct j2stbl_object {
      J2SOBJECT_DECLARE_OBJECT;
       // auto increase id, readonly, do not modify it!
      int __id__;
    };
    ```

对外提供以下 `api`:

1.  初始化相关
    -   struct j2stable \*j2stable_init(const char \*table, struct j2sobject_prototype \*proto);
        -   table: 表名字，默认我们会依据 `J2STBL_BASE_DB_PATH` 指定目录，去查找并且加载 `table.json` 文件；
            默认 `J2STBL_BASE_DB_PATH` 设置为当前目录下的 `./j2stbls` 子目录，你可以在编译时指定该宏进行覆盖系统默认配置。
        -   proto: 用户 `j2sobject` 对象（数据记录）的原型；（这个原型是数组中对象 `[{}]` 的原型，不是数组 `[]` 原型）
    -   void j2stable_deinit(struct j2stable \*tbl);
        销毁数据，释放相关资源
2.  增删改查相关接口
    -   struct j2stbl_object\* j2stable_query_all(struct j2stable \*tbl);
        返回数据头节点 `J2S_ARRAY` 实际的数据需要通过 `j2sobject` 对象中的 `next/prev` 进行遍历。
        在得到返回值后需要判断是否为 `NULL`, 并且判断当前节点是否与 `next/prev` 节点一致，如果一致，说明只有头节点，没有实际数据。

        我们也提供了辅助函数： `j2stable_empty` 来协助进行判断。

    -   int j2stable_update(struct j2stable \*tbl, struct j2stbl_object \*self);
        对 `j2stable_query_all` 返回的链表中元素进行修改后，可以通过该接口将数据写入表中。

        其实，这里的 `self` 参数并没有实际作用。

    -   int j2stable_delete(struct j2stable \*tbl, struct j2stbl_object \*self);
        删除 `j2stable_query_all` 返回的链表中元素，并且更新到表中。

        `self` 必须是链表中元素
    -   int j2stable_insert(struct j2stable \*tbl, struct j2stbl_object \*self);
        插入新的数据，并且更新到表中， `self` 是 `j2sobject_create` 创建的对象（必须是 `j2stbl_object` 子对象）。

        新的对象将被插入到队尾，并且依据前一个元素生成自增 `id`;( 第一个数据 `id` 为 `1`)

        注意该函数调用后， `j2stable` 会自动管理 `self` 对象的析构，请不要人为释放其空间。
3.  辅助函数
    -   int j2stable_version(struct j2stable \*tbl);
        返回当前表的版本号，默认不支持，仅当 `J2STBL_STORE_METADATA` 被设置为 `1` 是有效；
    -   int j2stable_empty(struct j2stable \*tbl);
        判断数据是否为空


## 使用说明

下面以简单的示例来解释使用方法：

```c


#define _MACTBL_DATA_OFFSET(ele) offsetof(j2stbl_mactbl_t, ele)
#define _MACTBL_DATA_LEN(ele) sizeof(((j2stbl_mactbl_t*)0)->ele)

// 定义 j2stbl_object 数据结构体
typedef struct {
    // 首先，引入 struct j2stbl_object
    J2STBL_DECLARE_OBJECT;
    // 用户字段，自己维护的 id
    int id;
    // 用户字段，代表 mac 地址
    char mac[18];
    // 用户字段
    int type;
} j2stbl_mactbl_t;

// 定义用户结构体原型相关数据
static int j2stbl_mactbl_ctor(struct j2sobject* obj);

struct j2sobject_prototype j2stbl_mac_tbl_prototype = {.name = "mac_tbl",
                                                       .type = J2S_OBJECT,
                                                       .size = sizeof(
                                                           j2stbl_mactbl_t),
                                                       .ctor = j2stbl_mactbl_ctor,
                                                       .dtor = NULL

};

// 定义用户结构体字段信息
static struct j2sobject_fields_prototype _j2stbl_mactbl_fields_prototype[] = {
    // 这里首先通过提供的 J2STBL_OBJECT_PRIV_FIELDS 来引入 j2stable 对象内部字段
    J2STBL_OBJECT_PRIV_FIELDS,
    {.name = "id", .type = J2S_INT, .offset = _MACTBL_DATA_OFFSET(id), .offset_len = 0},
    {.name = "mac", .type = J2S_STRING, .offset = _MACTBL_DATA_OFFSET(mac), .offset_len = _MACTBL_DATA_LEN(mac)},
    {.name = "type", .type = J2S_INT, .offset = _MACTBL_DATA_OFFSET(type), .offset_len = 0},
    {0}};

```

下面看下数据操作方法：

```c
    // 初始化数据表，获取句柄
    struct j2stable* tbl = j2stable_init("mac_tbl", &j2stbl_mac_tbl_prototype);
    if (!tbl) {
        printf("can not init mac tbl\n");
        return -1;
    }

    // 查询数据表
    struct j2stbl_object* data = j2stable_query_all(tbl);
    if (data != NULL) {
        // 对数据进行遍历
        for (t = (j2stbl_mactbl_t*)J2SOBJECT(data)->next; t != (j2stbl_mactbl_t*)J2SOBJECT(data); t = (j2stbl_mactbl_t*)J2SOBJECT(t)->next) {
            printf("mac tbl:%p\n", t);
            printf("mac tbl __id__:%d\n", J2STBL_OBJECT_SELF(t)->__id__);
            printf("mac tbl id:%d\n", t->id);
            printf("mac tbl mac:%s\n", t->mac);
            printf("mac tbl type:%d\n", t->type);
            if (t->id == 3) {
                del = t;
            }
        }
    }

    // 创建新的数据
    j2stbl_mactbl_t* item = (j2stbl_mactbl_t*)j2sobject_create(&j2stbl_mac_tbl_prototype);
    item->id = 10;
    item->type = 1;
    snprintf(item->mac, sizeof(item->mac), "%s", "6c:0b:84:3c:71:9e");
    // 插入新的数据， item 对应内存会交给 j2stable 管理，用户不需要再关系
    j2stable_insert(tbl, J2STBL_OBJECT_SELF(item));


    if (del) {
        printf("press any key delete:%p, which __id__:%d\n", del, J2STBL_OBJECT_SELF(del)->__id__);
        getchar();
        // 删除指定的数据
        j2stable_delete(tbl, J2STBL_OBJECT_SELF(del));
    }

    // 销毁数据表
    j2stable_deinit(tbl);
```
## 备注


