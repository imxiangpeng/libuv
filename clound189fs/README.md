# 

## 解析获取文件列表到 j2sobject 对象

我们写了模拟程序 `cloud_j2sobject.c`， 文件和目录列表以链表形式保存在 `j2sobject` 里面；（j2scloud_folder_resp.folderList, j2scloud_folder_resp.fileList）

最后遍历就可以拿到所有的文件和目录了。


## 成功获取到文件列表 step 03

我们利用 `alist` 参考 `WEB` 接口成功获取了文件列表；

> 因为云盘新的接口需要使用我们自己的私钥来进行加密，目前我们走不同，所以继续采用 `WEB` 方式来实现。


