
# j2sobject

> 孟祥朋 20240222

`j2sobject` 是一个可以将 `JSON` 对象与结构体直接进行互相转换的模块。


## 由来

最近在做路由器相关的一些工作，在实际开发过程中，涉及到很多的数据交互、数据存储等等，其中很多的都是以 `JSON` 的形式进行的。
在 `JSON` 的操作上，我们一般采用的是 `cJSON/json-c`, 虽然都比较简单，但是在实际代码编写时，会导致很多的冗余、繁重工作，同时也让代码质量以及可读性大大降低。

基于此，我考虑尝试写一段代码来实现将 `C` 语言结构体直接映射到 `JSON` ，或者将 `JSON` 对象直接映射为 `C` 语言结构体。
上面这两个过程我们称之为：“序列化”、“反序列化”！(主要接口都含有  `serialize/deserialize` )

最开始设计方案就是上面提到的 `JSON` `Object` 对象与结构体的映射，我们也定下了一些限制，例如不去支持数组等等。
但是随着使用的逐渐增多，我们对数组以及子数组等等也都进行了支持；

~~(需要说明，截至目前，我们仍然不支持基础类型数组，例如 `[1,2]/["str","string"]`)~~

PS.20240228 现在我们已经支持基本的 int/double 类型数组，我们将最后的 =offset_len= 作为数组的大小。

但是这个逻辑与我们对字符串的处理一样也不一样，如果经字符串理解为字符，那么是一样的。

> 注意，我们无法支持元素数量动态变化的数组，只支持固定大小数组。

我们如何处理字符串数组呢？

对于字符串如果也要采用这个逻辑支持数组的话，对于字符串类型就不能支持预分配内存形式了。

仔细思考我们觉得还是应该支持当前两种字符串处理方式。

那么如何支持字符串数组呢？添加一个新的类型吧；

还是我们继续不支持这种类型呢？

或许可以将类型 type 字段改成位运算，用 =J2S_STRING|J2S_ARRAY= 来表示字符串数组。

PS.20240306 今天决定尝试去支持字符串数组了，在实际过程中太容易遇到这种需求了。

对于基本类型 `int/double/string` 数组我们的处理逻辑就是在定义时采用与 `J2S_ARRAY` 或运算，通过 `offset_len` 指定数组大小。
(对于 `int/double` 支持直接使用 `J2S_INT/J2S_DOUBLE` 类型)

例如，下面是等效的：

```c
    {.name = "intarr", .type = J2S_INT, .offset = _J2SOBJECT_CRONTASK_DATA_OFFSET(intarr), .offset_len = _J2SOBJECT_CRONTASK_DATA_ARRAY_LEN(intarr)},
    {.name = "intarr", .type = J2S_INT | J2S_ARRAY, .offset = _J2SOBJECT_CRONTASK_DATA_OFFSET(intarr), .offset_len = _J2SOBJECT_CRONTASK_DATA_ARRAY_LEN(intarr)},
```

而对于字符串数组，因为我们已经将 `ofset_len` 是否为 `0` 作为判断是否是预分配内存了，所以在声明时必须指定 `| J2S_ARRAY`:

```c
    {.name = "argv", .type = J2S_ARRAY | J2S_STRING, .offset = _J2SOBJECT_CRONTASK_DATA_OFFSET(argv), .offset_len = _J2SOBJECT_CRONTASK_DATA_ARRAY_LEN(argv) /*string buffer will dynamic allocated when needed*/},
```

## 设计理念

在 `j2sobject` 的设计中，我们遵循两个原则：

-   不需要关心应用场景
-   不去关心结构体定义

只有这样，我们才能够做到统一、一致性！


## 设计原理

为了达到上面提到的两个原则，我们在创建对象时需要传递结构体原型（protol），为此，我们定义了以下结构体：

```c
struct j2sobject_prototype {
    const char *name;
    enum j2stype type; // object type
    int size;
    // only used when it's object not normal basic type such int/string/...
    // so we can create new dynamic object
    int (*ctor)(struct j2sobject *);
    // when offset_len > 0 it means that it's not pointer, data have been allocated
    // you should directly setup proto and other fields
    // care that do not modify any other none j2cobject fields which maybe causing loss data
    //int (*init)(struct j2sobject *);
    int (*dtor)(struct j2sobject *);
};
```

相关字段含义如下：

1.  `name`: 对象名字，请采用常量字符串， `j2sobject` 销毁时，不会考虑该字段；
2.  `type`: 对象类型，可以取值为 `J2S_OBJECT/J2S_ARRAY`; `enum j2stype` 在其他地方也有使用，表示相关字段类型；
3.  `size`: 用户结构体大小，用户定义结构体需要包含 `struct j2sobject` 成员，为了方便在定义时请采用宏引入： `J2SOBJECT_DECLARE_OBJECT`;
4.  `ctor`: 对象构造回调，当创建对象时，会自动调用该函数，在这里我们常常需要告知 `j2sobject` 当前对象都有哪些字段以及他们的类型等信息；
    对于结构体字段，对象存在两种情况：

    1.  内存已分配；
    2.  内存未分配；

    在反序列化过程中，对于内存已分配情况，我们会去确认相关 `proto` 是否有效，如果无效的话，我们仍然会调用 `ctor` 这点需要特别注意。

    思考：如何识别内存已分配还是未分配（需要动态申请）？
    参考 `struct j2sobject_fields_prototype` 中的 `offset_len` 字段；
5.  `dtor`: 对象析构函数，在对象销毁时被调用；

到目前为止，我们可以正常为用户结构体申请内存了，并且通知用户代码来对实现结构体的构造和析构了。

但是用户结构体的具体字段内容以及字段类型对我们而言还是不知道的。（C 不像 java/C++ 等等可以逆向找到结构体字段以及类型）
所以，接下来我们需要让用户在 `ctor` 中给我们传递字段相关信息；

```c
struct j2sobject_fields_prototype {
    const char *name;
    int type; // object type
    uint32_t offset;
    uint32_t offset_len;

    // should not null when cur field is object
    struct j2sobject_prototype *proto;
};
```

这里我们引入了 `struct j2sobject_fields_prototype` 来表示结构体中的一个字段。

因为结构体会包含很多字段，所以，我们需要用户给我们传递 `struct j2sobject_fields_prototype` 数组；

字段含义：

1.  `name`: 字段名字，需要与 `json` 中字段一致， 请采用常量字符串， `j2sobject` 销毁时，不会考虑该字段；
2.  `type`: 字段类型，可以取值为 `J2S_INT/J2S_DOUBLE/J2S_STRING/J2S_OBJECT/J2S_ARRAY`, 以及基础类型（`J2S_INT/J2S_DOUBLE/J2S_STRING`）与 `|J2S_ARRAY` 运算表示对应类型的数组;
3.  `offset`: 该字段在用户结构体中偏移；我们通过 `offsetof` 来计算；
4.  `offset_len`: 该字段大小，对于基础类型设置 `0` 就可以了。但是对于 `J2S_STRING/J2S_OBJECT/J2S_ARRAY` 类型，该字段如果为 `0` 意味着我们需要动态申请内存，如果不为 `0`, 那么意味着该字段对应的内存已经申请；
    通俗表达：
    1.  对于 `J2S_STRING` 字符串类型，如果用户结构提当前字段为 `char*` 那么请将该字段 `offset_len` 设置为 `0`, 如果是 `char []` 数组，请正确设置数组大小。
    2.  对于 `J2S_OBJECT/J2S_ARRAY` 为 `0` 意味着存放的是 `struct j2sobject*` 否则存放的是字段对应的用户结构体 `struct j2sobjectuser` 。
    3.  对于 `J2S_INT/J2S_DOUBLE` 类型，如果 `offset_len > 0` 那么这个时候我们认为这个字段是数组，数组大小就是 `offset_len`;
    4.  当基础类型（`J2S_INT/J2S_DOUBLE/J2S_STRING`）与 `|J2S_ARRAY` 时，表示该字段是数组，也必须指明 `offset_len` 作为数组大小。
    我们在处理数组类型是，`J2S_INT/J2S_DOUBLE/J2S_STRING` 分别遇到 `INT_MAX/NAN/NULL` 时我们就自动停止序列化，这样不会将无效的值写入。

5.  `proto`: 当前字段在构造时对应的原型，该字段对于基础类型 `J2S_INT/J2S_DOUBLE/J2S_STRING` 是不需要的，禁当当前字段是 `J2S_OBJECT/J2S_ARRAY` 时必须传递。

好了，通过这些信息，我们就可以知道用户结构体中的所有字段以及他们在内存中的布局信息了，依据 `offset` 我们可以准确的访问该字段！

这就是我们整个设计原理。


## J2SOBJECT API

j2sobject 对外提供 api 分为三类：

1.  创建、销毁
    -   struct j2sobject *j2sobject_create(struct j2sobject_prototype *proto);
        创建对象， `proto` 是该对象的原型
    -   struct j2sobject **j2sobject_create_array(struct j2sobject_prototype *proto);
        创建数组对象， `proto` 是该数组 **子对象** 的原型
    -   void j2sobject_free(struct j2sobject *self);
        销毁对象，以及其子对象
    -   int j2sobject_reset(struct j2sobject *self);
        清除对象用户数据；
2.  序列化
    该部分提供了将 `j2sobject` 对象进行序列化的操作：
    -   char **j2sobject_serialize(struct j2sobject *self);
        将 `j2sobject` 序列化为字符串；（ **注意：** 需要自行释放内存）
    -   int j2sobject_serialize_cjson(struct j2sobject *self, struct cJSON *target);
        将 `j2sobject` 对象序列化转换为 cJSON 对象；使用该接口需要特别注意，创建的 `cJSON` 必须与 `j2sobject` 类型一致；
    -   int j2sobject_serialize_file(struct j2sobject *self, const char *path);
        将 `j2sobject` 对象序列化保存到文件；
3.  反序列化
    -   int j2sobject_deserialize(struct j2sobject *self, const char *jstr);
        将 `JSON` 字符串，序列化为 `j2sobject` 对象；
    -   int j2sobject_deserialize_cjson(struct j2sobject *self, struct cJSON *jobj);
        将 `cJSON` 对象，序列化为 `j2sobject` 对象；
    -   int j2sobject_deserialize_file(struct j2sobject *self, const char *path);
        从 `JSON` 文件，序列化 `j2sobject` 对象。


## 使用说明

从上面介绍中，我们可以看到，要想使用 `j2sobject`, 我们需要首先定义：

1.  通过 `J2SOBJECT_DECLARE_OBJECT` 定义用户结构体

    ```c
    struct j2sobject_demo {
        J2SOBJECT_DECLARE_OBJECT;
        int intval;
        int intarr[4];
        char prealloc_string[128];
        char* dynamic_str;
        char *argv[10];  // max args
    };
    ```

    我们首先需要定义一个数据结构体，并且将 `J2SOBJECT_DECLARE_OBJECT` 添加为结构体第一个成员；

    然后依次定义 `json` 对应的字段。

    对于字符串类型，需要特别注意，有两种表述方式，分别代表已分配大小与动态分配两种字符串。
    (这两种在 `struct j2sobject_fields_prototype` 中的 `offset_len` 字段上有差异)

    **注意，字符串数组在定义时，必须采用 `char* name[size]` 的形式。**

2.  定义结构体原型 `struct j2sobject_prototype`
    我们上面已经提到，具体参考 [设计原理](#设计原理) 。

    对于简单的数据类型，通常我们不会使用到 `dtor`, 所以这个字段可以为空。

    但是如果自己使用到复杂或者自己控制内存分配的时候，必须要实现该字段。

    ```c
    static int j2sobject_demo_ctor(struct j2sobject *obj) {
        if (!obj)
            return -1;
        obj->name = "demo";
        obj->field_protos = _j2sobject_demo_fields_prototype;
        return 0;
    }
    static struct j2sobject_prototype _j2sobject_demo_prototype = {
        .name = "demo",
        .type = J2S_OBJECT,
        .size = sizeof(struct j2sobject_demo),
        .ctor = j2sobject_demo_ctor,
        .dtor = NULL
    };

    ```
3.  `struct j2sobject_fields_prototype`
    这里我们需要依据数据类型（结构体字段以及类型）来描述字段原型：

    ```c
    static struct j2sobject_fields_prototype _j2sobject_demo_fields_prototype[] = {
        {.name = "intval",          .type = J2S_INT,                .offset = _J2SOBJECT_DEMO_DATA_OFFSET(intval),           .offset_len = 0},
        // {.name = "intarr",       .type = J2S_INT,                .offset = _J2SOBJECT_DEMO_DATA_OFFSET(intarr), .offset_len = _J2SOBJECT_DEMO_DATA_ARRAY_LEN(intarr)},
        {.name = "intarr",          .type = J2S_INT | J2S_ARRAY,    .offset = _J2SOBJECT_DEMO_DATA_OFFSET(intarr), .offset_len = _J2SOBJECT_DEMO_DATA_ARRAY_LEN(intarr)},
        {.name = "prealloc_str",    .type = J2S_STRING,             .offset = _J2SOBJECT_DEMO_DATA_OFFSET(prealloc_str),     .offset_len = _J2SOBJECT_DEMO_DATA_LEN(prealloc_str) /*string buffer has been allocated*/},
        {.name = "dynamic_str",     .type = J2S_STRING,             .offset = _J2SOBJECT_DEMO_DATA_OFFSET(dynamic_str),      .offset_len = 0 /*string buffer will dynamic allocated when needed*/},
        {.name = "argv",            .type = J2S_ARRAY | J2S_STRING, .offset = _J2SOBJECT_CRONTASK_DATA_OFFSET(argv), .offset_len = _J2SOBJECT_DEMO_DATA_ARRAY_LEN(argv) /*string buffer will dynamic allocated when needed*/},
        {0}
    };
    ```

    为了便于书写，这里我们定义了两个辅助宏定义：

    ```c
    // 获取元素 ele 在结构体中偏移量
    #define _J2SOBJECT_DEMO_DATA_OFFSET(ele) \
        offsetof(struct j2sobject_demo, ele)

    // 获取结构体中 ele 元素大小
    #define _J2SOBJECT_DEMO_DATA_LEN(ele) \
        sizeof(((struct j2sobject_demo *)0)->ele)
    ```

    **特别说明：**

    -   `name` 字段，必须与 `json` 中字段一致；
    -   `offset_len` 字段，对象已分配请填写实际大小，需要动态分配时填 `0`;
        例如，上面 `prealloc_str` 与 `dynamic_str` 差异。

    其他部分参考 [设计原理](#设计原理) 章节；

4.  创建和使用对象

    ```c
      // 创建对象
      struct j2sobject_demo* object = (struct j2sobject_demo*)j2sobject_create(&_j2sobject_demo_prototype);

      // 将 json 文件  1.json 反序列化到对象
      j2sobject_deserialize_file(J2SOBJECT(object), "1.json");

      // 访问 struct j2sobject_demo* 中相关字段或者作出修改

      // 将对象重新序列化为可打印字符串
      char *str = j2sobject_serialize(J2SOBJECT(object));
      printf("serialize:%s\n", str);
      // 注意，内存必须手动释放
      free(str);

      // 将对象重新序列化保存到文件
      j2sobject_serialize_file(J2SOBJECT(object), "1-modified.json");

      // 释放对象
      j2sobject_free(J2SOBJECT(object))
    ```


## 备注

目前并没有完全支持 json 所有格式，我们主要工作就是与结构体的转换，所以大部分适配的内容都是 `JSON Object` 对象。

对于一些基础类型数组： `[1,2]/["abc","123"]` 我们是没有做支持的，目前仅支持如下数组格式 `[{},{}]`


