---
sidebar_label: Telegraf
title: Telegraf for TDengine Cloud
description: Write data into TDengine from telegraf.
---

Telegraf is an open-source, metrics collection software. Telegraf can collect the operation information of various components without having to write any scripts to collect regularly, reducing the difficulty of data acquisition.

Telegraf's data can be written to TDengine by simply adding the output configuration of Telegraf to the URL corresponding to taosAdapter and modifying several configuration items. The presence of Telegraf data in TDengine can take advantage of TDengine's efficient storage query performance and clustering capabilities for time-series data.

## Prerequisiteis

Before telegraf can write data into TDengine cloud service, you need to firstly manually create a database. Log in TDengine Cloud, click "Explorer" on the left navigation bar, then click the "+" button besides "Databases" to add a database named as "telegraf" using all default parameters.

## Install Telegraf

Supposed that you use Ubuntu system:

```bash
{{#include docs/examples/thirdparty/install-telegraf.sh:null:nrc}}
```

After installation, telegraf service should have been started. Lets stop it:

```bash
sudo systemctl stop telegraf
```

For installation instructions on other platforms please refer to the [official documentation](https://docs.influxdata.com/telegraf/v1.23/install/).

## Configure


Run this command in your terminal to save TDengine cloud token and URL as variables:

```bash
export TDENGINE_CLOUD_URL="<url>"
export TDENGINE_CLOUD_TOKEN="<token>"
```

<!-- exclude -->
You are expected to replace `<url>` and `<token>` with real TDengine cloud URL and token. To obtain the real values, please log in [TDengine Cloud](https://cloud.tdengine.com).
<!-- exclude-end -->


Then run this command to generate new telegraf.conf.

```bash
{{#include docs/examples/thirdparty/gen-telegraf-conf.sh:null:nrc}}
```

Edit section "outputs.http".

```toml
{{#include docs/examples/thirdparty/telegraf-conf.toml:null:nrc}}
```

The resulting configuration will collect CPU and memory data and sends it to TDengine database named "telegraf". Database "telegraf" will be created automatically if it dose not exist in advance.

## Start Telegraf

Start telegraf using new generated telegraf.conf file.

```bash
telegraf --config telegraf.conf
```

## Verify

- Check weather database "telegraf" exist by executing:

```sql
show databases;
```
![TDengine show telegraf databases](./telegraf-show-databases.webp)

Check weather super table cpu and mem exist:

```sql
show telegraf.stables;
```

![TDengine Cloud show telegraf stables](./telegraf-show-stables.webp)



