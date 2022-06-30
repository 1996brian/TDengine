/*
 * Copyright (c) 2019 TAOS Data, Inc. <jhtao@taosdata.com>
 *
 * This program is free software: you can use, redistribute, and/or modify
 * it under the terms of the GNU Affero General Public License, version 3
 * or later ("AGPL"), as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "tq.h"

int32_t tqInit() {
  int8_t old;
  while (1) {
    old = atomic_val_compare_exchange_8(&tqMgmt.inited, 0, 2);
    if (old != 2) break;
  }

  if (old == 0) {
    tqMgmt.timer = taosTmrInit(10000, 100, 10000, "TQ");
    if (tqMgmt.timer == NULL) {
      atomic_store_8(&tqMgmt.inited, 0);
      return -1;
    }
    atomic_store_8(&tqMgmt.inited, 1);
  }
  return 0;
}

void tqCleanUp() {
  int8_t old;
  while (1) {
    old = atomic_val_compare_exchange_8(&tqMgmt.inited, 1, 2);
    if (old != 2) break;
  }

  if (old == 1) {
    taosTmrCleanUp(tqMgmt.timer);
    atomic_store_8(&tqMgmt.inited, 0);
  }
}

STQ* tqOpen(const char* path, SVnode* pVnode, SWal* pWal) {
  STQ* pTq = taosMemoryCalloc(1, sizeof(STQ));
  if (pTq == NULL) {
    terrno = TSDB_CODE_TQ_OUT_OF_MEMORY;
    return NULL;
  }
  pTq->path = strdup(path);
  pTq->pVnode = pVnode;
  pTq->pWal = pWal;

  pTq->handles = taosHashInit(64, MurmurHash3_32, true, HASH_ENTRY_LOCK);

  pTq->pStreamTasks = taosHashInit(64, taosGetDefaultHashFunction(TSDB_DATA_TYPE_INT), true, HASH_NO_LOCK);

  pTq->pushMgr = taosHashInit(64, taosGetDefaultHashFunction(TSDB_DATA_TYPE_BIGINT), true, HASH_ENTRY_LOCK);

  if (tqMetaOpen(pTq) < 0) {
    ASSERT(0);
  }

  if (tqOffsetOpen(pTq) < 0) {
    ASSERT(0);
  }

  return pTq;
}

void tqClose(STQ* pTq) {
  if (pTq) {
    tqOffsetClose(pTq->pOffsetStore);
    taosHashCleanup(pTq->handles);
    taosHashCleanup(pTq->pStreamTasks);
    taosHashCleanup(pTq->pushMgr);
    taosMemoryFree(pTq->path);
    tqMetaClose(pTq);
    taosMemoryFree(pTq);
  }
}

int32_t tqSendMetaPollRsp(STQ* pTq, const SRpcMsg* pMsg, const SMqPollReq* pReq, const SMqMetaRsp* pRsp) {
  int32_t tlen = sizeof(SMqRspHead) + tEncodeSMqMetaRsp(NULL, pRsp);
  void*   buf = rpcMallocCont(tlen);
  if (buf == NULL) {
    return -1;
  }

  ((SMqRspHead*)buf)->mqMsgType = TMQ_MSG_TYPE__POLL_META_RSP;
  ((SMqRspHead*)buf)->epoch = pReq->epoch;
  ((SMqRspHead*)buf)->consumerId = pReq->consumerId;

  void* abuf = POINTER_SHIFT(buf, sizeof(SMqRspHead));
  tEncodeSMqMetaRsp(&abuf, pRsp);

  SRpcMsg resp = {
      .info = pMsg->info,
      .pCont = buf,
      .contLen = tlen,
      .code = 0,
  };
  tmsgSendRsp(&resp);

  tqDebug("vg %d from consumer %ld (epoch %d) send rsp, res msg type %d, reqOffset: %ld, rspOffset: %ld",
          TD_VID(pTq->pVnode), pReq->consumerId, pReq->epoch, pRsp->resMsgType, pRsp->reqOffset, pRsp->rspOffset);

  return 0;
}

int32_t tqSendDataRsp(STQ* pTq, const SRpcMsg* pMsg, const SMqPollReq* pReq, const SMqDataRsp* pRsp) {
  ASSERT(taosArrayGetSize(pRsp->blockData) == pRsp->blockNum);
  ASSERT(taosArrayGetSize(pRsp->blockDataLen) == pRsp->blockNum);

  if (pRsp->withSchema) {
    ASSERT(taosArrayGetSize(pRsp->blockSchema) == pRsp->blockNum);
  } else {
    ASSERT(taosArrayGetSize(pRsp->blockSchema) == 0);
  }

  int32_t len;
  int32_t code;
  tEncodeSize(tEncodeSMqDataRsp, pRsp, len, code);
  if (code < 0) {
    return -1;
  }
  int32_t tlen = sizeof(SMqRspHead) + len;
  void*   buf = rpcMallocCont(tlen);
  if (buf == NULL) {
    return -1;
  }

  ((SMqRspHead*)buf)->mqMsgType = TMQ_MSG_TYPE__POLL_RSP;
  ((SMqRspHead*)buf)->epoch = pReq->epoch;
  ((SMqRspHead*)buf)->consumerId = pReq->consumerId;

  void* abuf = POINTER_SHIFT(buf, sizeof(SMqRspHead));

  SEncoder encoder;
  tEncoderInit(&encoder, abuf, len);
  tEncodeSMqDataRsp(&encoder, pRsp);
  /*tEncodeSMqDataBlkRsp(&abuf, pRsp);*/

  SRpcMsg rsp = {
      .info = pMsg->info,
      .pCont = buf,
      .contLen = tlen,
      .code = 0,
  };
  tmsgSendRsp(&rsp);

  char buf1[50];
  char buf2[50];
  tFormatOffset(buf1, 50, &pRsp->reqOffset);
  tFormatOffset(buf2, 50, &pRsp->rspOffset);
  tqDebug("vg %d from consumer %ld (epoch %d) send rsp, block num: %d, reqOffset: %s, rspOffset: %s",
          TD_VID(pTq->pVnode), pReq->consumerId, pReq->epoch, pRsp->blockNum, buf1, buf2);

  return 0;
}

int32_t tqProcessOffsetCommitReq(STQ* pTq, char* msg, int32_t msgLen) {
  STqOffset offset = {0};
  SDecoder  decoder;
  tDecoderInit(&decoder, msg, msgLen);
  if (tDecodeSTqOffset(&decoder, &offset) < 0) {
    ASSERT(0);
    return -1;
  }
  tDecoderClear(&decoder);

  if (offset.val.type == TMQ_OFFSET__SNAPSHOT_DATA) {
    tqDebug("receive offset commit msg to %s on vg %d, offset(type:snapshot) uid: %ld, ts: %ld", offset.subKey,
            TD_VID(pTq->pVnode), offset.val.uid, offset.val.ts);
  } else if (offset.val.type == TMQ_OFFSET__LOG) {
    tqDebug("receive offset commit msg to %s on vg %d, offset(type:log) version: %ld", offset.subKey,
            TD_VID(pTq->pVnode), offset.val.version);
  } else {
    ASSERT(0);
  }
  STqOffset* pOffset = tqOffsetRead(pTq->pOffsetStore, offset.subKey);
  if (pOffset == NULL || pOffset->val.version < offset.val.version) {
    if (tqOffsetWrite(pTq->pOffsetStore, &offset) < 0) {
      ASSERT(0);
      return -1;
    }
  }

  return 0;
}

static int32_t tqInitDataRsp(SMqDataRsp* pRsp, const SMqPollReq* pReq, int8_t subType) {
  pRsp->reqOffset = pReq->reqOffset;

  pRsp->blockData = taosArrayInit(0, sizeof(void*));
  pRsp->blockDataLen = taosArrayInit(0, sizeof(int32_t));

  if (pRsp->blockData == NULL || pRsp->blockDataLen == NULL) {
    return -1;
  }

  pRsp->withTbName = pReq->withTbName;
  if (pRsp->withTbName) {
    pRsp->blockTbName = taosArrayInit(0, sizeof(void*));
    if (pRsp->blockTbName == NULL) {
      // TODO free
      return -1;
    }
  }

  if (subType == TOPIC_SUB_TYPE__COLUMN) {
    pRsp->withSchema = false;
  } else {
    pRsp->withSchema = true;
    pRsp->blockSchema = taosArrayInit(0, sizeof(void*));
    if (pRsp->blockSchema == NULL) {
      // TODO free
      return -1;
    }
  }
  return 0;
}

static int32_t tqInitMetaRsp(SMqMetaRsp* pRsp, const SMqPollReq* pReq) { return 0; }

static FORCE_INLINE void tqOffsetResetToData(STqOffsetVal* pOffsetVal, int64_t uid, int64_t ts) {
  pOffsetVal->type = TMQ_OFFSET__SNAPSHOT_DATA;
  pOffsetVal->uid = uid;
  pOffsetVal->ts = ts;
}

static FORCE_INLINE void tqOffsetResetToLog(STqOffsetVal* pOffsetVal, int64_t ver) {
  pOffsetVal->type = TMQ_OFFSET__LOG;
  pOffsetVal->version = ver;
}

int32_t tqProcessPollReq(STQ* pTq, SRpcMsg* pMsg, int32_t workerId) {
  SMqPollReq*  pReq = pMsg->pCont;
  int64_t      consumerId = pReq->consumerId;
  int64_t      timeout = pReq->timeout;
  int32_t      reqEpoch = pReq->epoch;
  int32_t      code = 0;
  STqOffsetVal reqOffset = pReq->reqOffset;
  STqOffsetVal fetchOffsetNew;

  // 1.find handle
  char buf[50];
  tFormatOffset(buf, 50, &reqOffset);
  tqDebug("tmq poll: consumer %ld (epoch %d) recv poll req in vg %d, req offset %s", consumerId, pReq->epoch,
          TD_VID(pTq->pVnode), buf);

  STqHandle* pHandle = taosHashGet(pTq->handles, pReq->subKey, strlen(pReq->subKey));
  /*ASSERT(pHandle);*/
  if (pHandle == NULL) {
    tqError("tmq poll: no consumer handle for consumer %ld in vg %d, subkey %s", consumerId, TD_VID(pTq->pVnode),
            pReq->subKey);
    return -1;
  }

  // check rebalance
  if (pHandle->consumerId != consumerId) {
    tqError("tmq poll: consumer handle mismatch for consumer %ld in vg %d, subkey %s, handle consumer id %ld",
            consumerId, TD_VID(pTq->pVnode), pReq->subKey, pHandle->consumerId);
    return -1;
  }

  // update epoch if need
  int32_t consumerEpoch = atomic_load_32(&pHandle->epoch);
  while (consumerEpoch < reqEpoch) {
    consumerEpoch = atomic_val_compare_exchange_32(&pHandle->epoch, consumerEpoch, reqEpoch);
  }

  // 2.reset offset if needed
  if (reqOffset.type > 0) {
    fetchOffsetNew = reqOffset;
  } else {
    STqOffset* pOffset = tqOffsetRead(pTq->pOffsetStore, pReq->subKey);
    if (pOffset != NULL) {
      fetchOffsetNew = pOffset->val;
      char formatBuf[50];
      tFormatOffset(formatBuf, 50, &fetchOffsetNew);
      tqDebug("tmq poll: consumer %ld, offset reset to %s", consumerId, formatBuf);
    } else {
      if (reqOffset.type == TMQ_OFFSET__RESET_EARLIEAST) {
        if (pReq->useSnapshot) {
          if (!pHandle->fetchMeta) {
            tqOffsetResetToData(&fetchOffsetNew, 0, 0);
          } else {
            // reset to meta
            ASSERT(0);
          }
        } else {
          tqOffsetResetToLog(&fetchOffsetNew, walGetFirstVer(pTq->pVnode->pWal));
        }
      } else if (reqOffset.type == TMQ_OFFSET__RESET_LATEST) {
        tqOffsetResetToLog(&fetchOffsetNew, walGetLastVer(pTq->pVnode->pWal));
      } else if (reqOffset.type == TMQ_OFFSET__RESET_NONE) {
        tqError("tmq poll: no offset committed for consumer %ld in vg %d, subkey %s, reset none failed", consumerId,
                TD_VID(pTq->pVnode), pReq->subKey);
        terrno = TSDB_CODE_TQ_NO_COMMITTED_OFFSET;
        return -1;
      }
    }
  }

  // 3.query
  SMqDataRsp dataRsp = {0};
  tqInitDataRsp(&dataRsp, pReq, pHandle->execHandle.subType);

  if (fetchOffsetNew.type == TMQ_OFFSET__LOG) {
    int64_t   fetchVer = fetchOffsetNew.version + 1;
    SWalHead* pHeadWithCkSum = taosMemoryMalloc(sizeof(SWalHead) + 2048);
    if (pHeadWithCkSum == NULL) {
      return -1;
    }

    walSetReaderCapacity(pHandle->pWalReader, 2048);

    while (1) {
      consumerEpoch = atomic_load_32(&pHandle->epoch);
      if (consumerEpoch > reqEpoch) {
        tqWarn("tmq poll: consumer %ld (epoch %d) vg %d offset %ld, found new consumer epoch %d, discard req epoch %d",
               consumerId, pReq->epoch, TD_VID(pTq->pVnode), fetchVer, consumerEpoch, reqEpoch);
        break;
      }

      if (tqFetchLog(pTq, pHandle, &fetchVer, &pHeadWithCkSum) < 0) {
        // TODO add push mgr

        tqOffsetResetToLog(&dataRsp.rspOffset, fetchVer);
        ASSERT(dataRsp.rspOffset.version >= dataRsp.reqOffset.version);
        if (tqSendDataRsp(pTq, pMsg, pReq, &dataRsp) < 0) {
          code = -1;
        }
        goto OVER;
      }

      SWalReadHead* pHead = &pHeadWithCkSum->head;

      tqDebug("tmq poll: consumer %ld (epoch %d) iter log, vg %d offset %ld msgType %d", consumerId, pReq->epoch,
              TD_VID(pTq->pVnode), fetchVer, pHead->msgType);

      if (pHead->msgType == TDMT_VND_SUBMIT) {
        SSubmitReq* pCont = (SSubmitReq*)&pHead->body;

        if (tqLogScanExec(pTq, &pHandle->execHandle, pCont, &dataRsp, workerId) < 0) {
          /*ASSERT(0);*/
        }
        // TODO batch optimization:
        // TODO continue scan until meeting batch requirement
        if (dataRsp.blockNum > 0 /* threshold */) {
          tqOffsetResetToLog(&dataRsp.rspOffset, fetchVer);
          ASSERT(dataRsp.rspOffset.version >= dataRsp.reqOffset.version);

          if (tqSendDataRsp(pTq, pMsg, pReq, &dataRsp) < 0) {
            code = -1;
          }
          goto OVER;
        } else {
          fetchVer++;
        }

      } else {
        ASSERT(pHandle->fetchMeta);
        ASSERT(IS_META_MSG(pHead->msgType));
        tqInfo("fetch meta msg, ver: %ld, type: %d", pHead->version, pHead->msgType);
        SMqMetaRsp metaRsp = {0};
        metaRsp.reqOffset = pReq->reqOffset.version;
        /*tqOffsetResetToLog(&metaR)*/
        metaRsp.rspOffset = fetchVer;
        metaRsp.resMsgType = pHead->msgType;
        metaRsp.metaRspLen = pHead->bodyLen;
        metaRsp.metaRsp = pHead->body;
        if (tqSendMetaPollRsp(pTq, pMsg, pReq, &metaRsp) < 0) {
          code = -1;
          goto OVER;
        }
        code = 0;
        goto OVER;
      }
    }

    taosMemoryFree(pHeadWithCkSum);
  } else if (fetchOffsetNew.type == TMQ_OFFSET__SNAPSHOT_DATA) {
    // 1. set uid and ts
    // 2. get data (rebuild reader if needed)
    // 3. get new uid and ts

    char formatBuf[50];
    tFormatOffset(formatBuf, 50, &dataRsp.reqOffset);
    tqInfo("retrieve using snapshot req offset %s", formatBuf);
    if (tqScanSnapshot(pTq, &pHandle->execHandle, &dataRsp, workerId) < 0) {
      ASSERT(0);
    }

    // 4. send rsp
    if (dataRsp.blockNum != 0) {
      tqOffsetResetToData(&dataRsp.rspOffset, 0, 0);
      if (tqSendDataRsp(pTq, pMsg, pReq, &dataRsp) < 0) {
        code = -1;
      }
    }
  } else if (fetchOffsetNew.type == TMQ_OFFSET__SNAPSHOT_META) {
    ASSERT(0);
  }

OVER:
  // TODO wrap in destroy func
  taosArrayDestroy(dataRsp.blockDataLen);
  taosArrayDestroyP(dataRsp.blockData, (FDelete)taosMemoryFree);

  if (dataRsp.withSchema) {
    taosArrayDestroyP(dataRsp.blockSchema, (FDelete)tDeleteSSchemaWrapper);
  }

  if (dataRsp.withTbName) {
    taosArrayDestroyP(dataRsp.blockTbName, (FDelete)taosMemoryFree);
  }

  return code;
}

#if 0
int32_t tqProcessPollReq(STQ* pTq, SRpcMsg* pMsg, int32_t workerId) {
  SMqPollReq* pReq = pMsg->pCont;
  int64_t     consumerId = pReq->consumerId;
  int64_t     timeout = pReq->timeout;
  int32_t     reqEpoch = pReq->epoch;
  int64_t     fetchOffset;
  int32_t     code = 0;

  // get offset to fetch message
  if (pReq->currentOffset >= 0) {
    fetchOffset = pReq->currentOffset + 1;
  } else {
    STqOffset* pOffset = tqOffsetRead(pTq->pOffsetStore, pReq->subKey);
    if (pOffset != NULL) {
      ASSERT(pOffset->val.type == TMQ_OFFSET__LOG);
      tqDebug("consumer %ld, restore offset of %s on vg %d, offset(type:log) version: %ld", consumerId, pReq->subKey,
              TD_VID(pTq->pVnode), pOffset->val.version);
      fetchOffset = pOffset->val.version + 1;
    } else {
      if (pReq->currentOffset == TMQ_CONF__RESET_OFFSET__EARLIEAST) {
        fetchOffset = walGetFirstVer(pTq->pWal);
      } else if (pReq->currentOffset == TMQ_CONF__RESET_OFFSET__LATEST) {
        fetchOffset = walGetCommittedVer(pTq->pWal);
      } else if (pReq->currentOffset == TMQ_CONF__RESET_OFFSET__NONE) {
        tqError("tmq poll: no offset committed for consumer %ld in vg %d, subkey %s", consumerId, TD_VID(pTq->pVnode),
                pReq->subKey);
        terrno = TSDB_CODE_TQ_NO_COMMITTED_OFFSET;
        return -1;
      }
      tqDebug("consumer %ld, restore offset of %s on vg %d failed, config is %ld, set to %ld", consumerId, pReq->subKey,
              TD_VID(pTq->pVnode), pReq->currentOffset, fetchOffset);
    }
  }

  tqDebug("tmq poll: consumer %ld (epoch %d) recv poll req in vg %d, req offset %ld fetch offset %ld", consumerId,
          pReq->epoch, TD_VID(pTq->pVnode), pReq->currentOffset, fetchOffset);

  STqHandle* pHandle = taosHashGet(pTq->handles, pReq->subKey, strlen(pReq->subKey));
  /*ASSERT(pHandle);*/
  if (pHandle == NULL) {
    tqError("tmq poll: no consumer handle for consumer %ld in vg %d, subkey %s", consumerId, TD_VID(pTq->pVnode),
            pReq->subKey);
    return -1;
  }

  if (pHandle->consumerId != consumerId) {
    tqError("tmq poll: consumer handle mismatch for consumer %ld in vg %d, subkey %s, handle consumer id %ld",
            consumerId, TD_VID(pTq->pVnode), pReq->subKey, pHandle->consumerId);
    return -1;
  }

  int32_t consumerEpoch = atomic_load_32(&pHandle->epoch);
  while (consumerEpoch < reqEpoch) {
    consumerEpoch = atomic_val_compare_exchange_32(&pHandle->epoch, consumerEpoch, reqEpoch);
  }

  SMqDataBlkRsp rsp = {0};
  rsp.reqOffset = pReq->currentOffset;

  rsp.blockData = taosArrayInit(0, sizeof(void*));
  rsp.blockDataLen = taosArrayInit(0, sizeof(int32_t));

  if (rsp.blockData == NULL || rsp.blockDataLen == NULL) {
    return -1;
  }

  rsp.withTbName = pReq->withTbName;
  if (rsp.withTbName) {
    rsp.blockTbName = taosArrayInit(0, sizeof(void*));
  }

  if (pHandle->execHandle.subType == TOPIC_SUB_TYPE__COLUMN) {
    rsp.withSchema = false;
  } else {
    rsp.withSchema = true;
    rsp.blockSchema = taosArrayInit(0, sizeof(void*));
  }

#if 1
  if (pReq->useSnapshot) {
    // TODO set ver into snapshot
    int64_t lastVer = walGetCommittedVer(pTq->pWal);
    if (rsp.reqOffset < lastVer) {
      tqInfo("retrieve using snapshot req offset %ld last ver %ld", rsp.reqOffset, lastVer);
      tqScanSnapshot(pTq, &pHandle->execHandle, &rsp, workerId);

      if (rsp.blockNum != 0) {
        rsp.withTbName = false;
        rsp.rspOffset = lastVer;
        tqInfo("direct send by snapshot req offset %ld rsp offset %ld", rsp.reqOffset, rsp.rspOffset);
        fetchOffset = lastVer;
        goto SEND_RSP;
      }
    }
  }
#endif

  SWalHead* pHeadWithCkSum = taosMemoryMalloc(sizeof(SWalHead) + 2048);
  if (pHeadWithCkSum == NULL) {
    return -1;
  }

  walSetReaderCapacity(pHandle->pWalReader, 2048);

  while (1) {
    consumerEpoch = atomic_load_32(&pHandle->epoch);
    if (consumerEpoch > reqEpoch) {
      tqWarn("tmq poll: consumer %ld (epoch %d) vg %d offset %ld, found new consumer epoch %d, discard req epoch %d",
             consumerId, pReq->epoch, TD_VID(pTq->pVnode), fetchOffset, consumerEpoch, reqEpoch);
      break;
    }

    if (tqFetchLog(pTq, pHandle, &fetchOffset, &pHeadWithCkSum) < 0) {
      // TODO add push mgr
      break;
    }

    SWalReadHead* pHead = &pHeadWithCkSum->head;

    tqDebug("tmq poll: consumer %ld (epoch %d) iter log, vg %d offset %ld msgType %d", consumerId, pReq->epoch,
            TD_VID(pTq->pVnode), fetchOffset, pHead->msgType);

    if (pHead->msgType == TDMT_VND_SUBMIT) {
      SSubmitReq* pCont = (SSubmitReq*)&pHead->body;

      if (tqDataExec(pTq, &pHandle->execHandle, pCont, &rsp, workerId) < 0) {
        /*ASSERT(0);*/
      }
    } else {
      ASSERT(pHandle->fetchMeta);
      ASSERT(IS_META_MSG(pHead->msgType));
      tqInfo("fetch meta msg, ver: %ld, type: %d", pHead->version, pHead->msgType);
      SMqMetaRsp metaRsp = {0};
      metaRsp.reqOffset = pReq->currentOffset;
      metaRsp.rspOffset = fetchOffset;
      metaRsp.resMsgType = pHead->msgType;
      metaRsp.metaRspLen = pHead->bodyLen;
      metaRsp.metaRsp = pHead->body;
      if (tqSendMetaPollRsp(pTq, pMsg, pReq, &metaRsp) < 0) {
        code = -1;
        goto OVER;
      }
      code = 0;
      goto OVER;
    }

    // TODO batch optimization:
    // TODO continue scan until meeting batch requirement
    if (rsp.blockNum > 0 /* threshold */) {
      break;
    } else {
      fetchOffset++;
    }
  }

  taosMemoryFree(pHeadWithCkSum);

SEND_RSP:
  ASSERT(taosArrayGetSize(rsp.blockData) == rsp.blockNum);
  ASSERT(taosArrayGetSize(rsp.blockDataLen) == rsp.blockNum);
  if (rsp.withSchema) {
    ASSERT(taosArrayGetSize(rsp.blockSchema) == rsp.blockNum);
  }

  rsp.rspOffset = fetchOffset;

  if (tqSendDataRsp(pTq, pMsg, pReq, &rsp) < 0) {
    code = -1;
  }
OVER:
  // TODO wrap in destroy func
  taosArrayDestroy(rsp.blockDataLen);
  taosArrayDestroyP(rsp.blockData, (FDelete)taosMemoryFree);

  if (rsp.withSchema) {
    taosArrayDestroyP(rsp.blockSchema, (FDelete)tDeleteSSchemaWrapper);
  }

  if (rsp.withTbName) {
    taosArrayDestroyP(rsp.blockTbName, (FDelete)taosMemoryFree);
  }

  return code;
}
#endif

int32_t tqProcessVgDeleteReq(STQ* pTq, char* msg, int32_t msgLen) {
  SMqVDeleteReq* pReq = (SMqVDeleteReq*)msg;

  int32_t code = taosHashRemove(pTq->handles, pReq->subKey, strlen(pReq->subKey));
  ASSERT(code == 0);

  if (tqMetaDeleteHandle(pTq, pReq->subKey) < 0) {
    ASSERT(0);
  }
  return 0;
}

int32_t tqProcessVgChangeReq(STQ* pTq, char* msg, int32_t msgLen) {
  SMqRebVgReq req = {0};
  tDecodeSMqRebVgReq(msg, &req);
  // todo lock
  STqHandle* pHandle = taosHashGet(pTq->handles, req.subKey, strlen(req.subKey));
  if (pHandle == NULL) {
    ASSERT(req.oldConsumerId == -1);
    ASSERT(req.newConsumerId != -1);
    STqHandle tqHandle = {0};
    pHandle = &tqHandle;
    /*taosInitRWLatch(&pExec->lock);*/

    memcpy(pHandle->subKey, req.subKey, TSDB_SUBSCRIBE_KEY_LEN);
    pHandle->consumerId = req.newConsumerId;
    pHandle->epoch = -1;

    pHandle->execHandle.subType = req.subType;
    pHandle->fetchMeta = req.withMeta;

    pHandle->pWalReader = walOpenReadHandle(pTq->pVnode->pWal);
    for (int32_t i = 0; i < 5; i++) {
      pHandle->execHandle.pExecReader[i] = tqInitSubmitMsgScanner(pTq->pVnode->pMeta);
    }
    if (pHandle->execHandle.subType == TOPIC_SUB_TYPE__COLUMN) {
      pHandle->execHandle.execCol.qmsg = req.qmsg;
      req.qmsg = NULL;
      for (int32_t i = 0; i < 5; i++) {
        SReadHandle handle = {
            .reader = pHandle->execHandle.pExecReader[i],
            .meta = pTq->pVnode->pMeta,
            .vnode = pTq->pVnode,
        };
        pHandle->execHandle.execCol.task[i] = qCreateStreamExecTaskInfo(pHandle->execHandle.execCol.qmsg, &handle);
        ASSERT(pHandle->execHandle.execCol.task[i]);
      }
    } else if (pHandle->execHandle.subType == TOPIC_SUB_TYPE__DB) {
      pHandle->execHandle.execDb.pFilterOutTbUid =
          taosHashInit(64, taosGetDefaultHashFunction(TSDB_DATA_TYPE_BIGINT), false, HASH_NO_LOCK);
    } else if (pHandle->execHandle.subType == TOPIC_SUB_TYPE__TABLE) {
      pHandle->execHandle.execTb.suid = req.suid;
      SArray* tbUidList = taosArrayInit(0, sizeof(int64_t));
      tsdbGetCtbIdList(pTq->pVnode->pMeta, req.suid, tbUidList);
      tqDebug("vg %d, tq try get suid: %ld", TD_VID(pTq->pVnode), req.suid);
      for (int32_t i = 0; i < taosArrayGetSize(tbUidList); i++) {
        int64_t tbUid = *(int64_t*)taosArrayGet(tbUidList, i);
        tqDebug("vg %d, idx %d, uid: %ld", TD_VID(pTq->pVnode), i, tbUid);
      }
      for (int32_t i = 0; i < 5; i++) {
        tqReadHandleSetTbUidList(pHandle->execHandle.pExecReader[i], tbUidList);
      }
      taosArrayDestroy(tbUidList);
    }
    taosHashPut(pTq->handles, req.subKey, strlen(req.subKey), pHandle, sizeof(STqHandle));
    if (tqMetaSaveHandle(pTq, req.subKey, pHandle) < 0) {
      // TODO
    }
  } else {
    /*ASSERT(pExec->consumerId == req.oldConsumerId);*/
    // TODO handle qmsg and exec modification
    atomic_store_32(&pHandle->epoch, -1);
    atomic_store_64(&pHandle->consumerId, req.newConsumerId);
    atomic_add_fetch_32(&pHandle->epoch, 1);
    if (tqMetaSaveHandle(pTq, req.subKey, pHandle) < 0) {
      // TODO
    }
  }

  return 0;
}

int32_t tqProcessTaskDeployReq(STQ* pTq, char* msg, int32_t msgLen) {
  SStreamTask* pTask = taosMemoryCalloc(1, sizeof(SStreamTask));
  if (pTask == NULL) {
    return -1;
  }
  SDecoder decoder;
  tDecoderInit(&decoder, (uint8_t*)msg, msgLen);
  if (tDecodeSStreamTask(&decoder, pTask) < 0) {
    ASSERT(0);
  }
  tDecoderClear(&decoder);
  ASSERT(pTask->isDataScan == 0 || pTask->isDataScan == 1);
  if (pTask->isDataScan == 0 && pTask->sinkType == TASK_SINK__NONE) {
    ASSERT(taosArrayGetSize(pTask->childEpInfo) != 0);
  }

  pTask->execStatus = TASK_EXEC_STATUS__IDLE;

  pTask->inputQueue = streamQueueOpen();
  pTask->outputQueue = streamQueueOpen();
  pTask->inputStatus = TASK_INPUT_STATUS__NORMAL;
  pTask->outputStatus = TASK_OUTPUT_STATUS__NORMAL;

  if (pTask->inputQueue == NULL || pTask->outputQueue == NULL) goto FAIL;

  pTask->pMsgCb = &pTq->pVnode->msgCb;

  // exec
  if (pTask->execType != TASK_EXEC__NONE) {
    // expand runners
    if (pTask->isDataScan) {
      SStreamReader* pStreamReader = tqInitSubmitMsgScanner(pTq->pVnode->pMeta);
      SReadHandle    handle = {
             .reader = pStreamReader,
             .meta = pTq->pVnode->pMeta,
             .vnode = pTq->pVnode,
      };
      /*pTask->exec.inputHandle = pStreamReader;*/
      pTask->exec.executor = qCreateStreamExecTaskInfo(pTask->exec.qmsg, &handle);
      ASSERT(pTask->exec.executor);
    } else {
      pTask->exec.executor = qCreateStreamExecTaskInfo(pTask->exec.qmsg, NULL);
      ASSERT(pTask->exec.executor);
    }
  }

  // sink
  /*pTask->ahandle = pTq->pVnode;*/
  if (pTask->sinkType == TASK_SINK__SMA) {
    pTask->smaSink.vnode = pTq->pVnode;
    pTask->smaSink.smaSink = smaHandleRes;
  } else if (pTask->sinkType == TASK_SINK__TABLE) {
    pTask->tbSink.vnode = pTq->pVnode;
    pTask->tbSink.tbSinkFunc = tqTableSink;

    ASSERT(pTask->tbSink.pSchemaWrapper);
    ASSERT(pTask->tbSink.pSchemaWrapper->pSchema);

    pTask->tbSink.pTSchema =
        tdGetSTSChemaFromSSChema(&pTask->tbSink.pSchemaWrapper->pSchema, pTask->tbSink.pSchemaWrapper->nCols);
    ASSERT(pTask->tbSink.pTSchema);
  }

  streamSetupTrigger(pTask);

  tqInfo("deploy stream task id %d child id %d on vg %d", pTask->taskId, pTask->selfChildId, TD_VID(pTq->pVnode));

  taosHashPut(pTq->pStreamTasks, &pTask->taskId, sizeof(int32_t), &pTask, sizeof(void*));

  return 0;
FAIL:
  if (pTask->inputQueue) streamQueueClose(pTask->inputQueue);
  if (pTask->outputQueue) streamQueueClose(pTask->outputQueue);
  if (pTask) taosMemoryFree(pTask);
  return -1;
}

int32_t tqProcessStreamTrigger(STQ* pTq, SSubmitReq* pReq) {
  void*              pIter = NULL;
  bool               failed = false;
  SStreamDataSubmit* pSubmit = NULL;

  pSubmit = streamDataSubmitNew(pReq);
  if (pSubmit == NULL) {
    failed = true;
  }

  while (1) {
    pIter = taosHashIterate(pTq->pStreamTasks, pIter);
    if (pIter == NULL) break;
    SStreamTask* pTask = *(SStreamTask**)pIter;
    if (atomic_load_8(&pTask->taskStatus) == TASK_STATUS__DROPPING) {
      continue;
    }
    if (!pTask->isDataScan) continue;

    if (!failed) {
      if (streamTaskInput(pTask, (SStreamQueueItem*)pSubmit) < 0) {
        continue;
      }

      if (streamLaunchByWrite(pTask, TD_VID(pTq->pVnode)) < 0) {
        continue;
      }
    } else {
      streamTaskInputFail(pTask);
    }
  }

  if (pSubmit) {
    streamDataSubmitRefDec(pSubmit);
    taosFreeQitem(pSubmit);
  }

  return failed ? -1 : 0;
}

int32_t tqProcessTaskRunReq(STQ* pTq, SRpcMsg* pMsg) {
  //
  SStreamTaskRunReq* pReq = pMsg->pCont;
  int32_t            taskId = pReq->taskId;
  SStreamTask*       pTask = *(SStreamTask**)taosHashGet(pTq->pStreamTasks, &taskId, sizeof(int32_t));
  if (atomic_load_8(&pTask->taskStatus) != TASK_STATUS__NORMAL) {
    return 0;
  }
  streamProcessRunReq(pTask);
  return 0;
}

int32_t tqProcessTaskDispatchReq(STQ* pTq, SRpcMsg* pMsg) {
  char*              msgStr = pMsg->pCont;
  char*              msgBody = POINTER_SHIFT(msgStr, sizeof(SMsgHead));
  int32_t            msgLen = pMsg->contLen - sizeof(SMsgHead);
  SStreamDispatchReq req;
  SDecoder           decoder;
  tDecoderInit(&decoder, msgBody, msgLen);
  tDecodeStreamDispatchReq(&decoder, &req);
  int32_t      taskId = req.taskId;
  SStreamTask* pTask = *(SStreamTask**)taosHashGet(pTq->pStreamTasks, &taskId, sizeof(int32_t));
  if (atomic_load_8(&pTask->taskStatus) != TASK_STATUS__NORMAL) {
    return 0;
  }
  SRpcMsg rsp = {
      .info = pMsg->info,
      .code = 0,
  };
  streamProcessDispatchReq(pTask, &req, &rsp);
  return 0;
}

int32_t tqProcessTaskRecoverReq(STQ* pTq, SRpcMsg* pMsg) {
  SStreamTaskRecoverReq* pReq = pMsg->pCont;
  int32_t                taskId = pReq->taskId;
  SStreamTask*           pTask = *(SStreamTask**)taosHashGet(pTq->pStreamTasks, &taskId, sizeof(int32_t));
  if (atomic_load_8(&pTask->taskStatus) != TASK_STATUS__NORMAL) {
    return 0;
  }
  streamProcessRecoverReq(pTask, pReq, pMsg);
  return 0;
}

int32_t tqProcessTaskDispatchRsp(STQ* pTq, SRpcMsg* pMsg) {
  SStreamDispatchRsp* pRsp = POINTER_SHIFT(pMsg->pCont, sizeof(SMsgHead));
  int32_t             taskId = pRsp->taskId;
  SStreamTask*        pTask = *(SStreamTask**)taosHashGet(pTq->pStreamTasks, &taskId, sizeof(int32_t));
  if (atomic_load_8(&pTask->taskStatus) != TASK_STATUS__NORMAL) {
    return 0;
  }
  streamProcessDispatchRsp(pTask, pRsp);
  return 0;
}

int32_t tqProcessTaskRecoverRsp(STQ* pTq, SRpcMsg* pMsg) {
  SStreamTaskRecoverRsp* pRsp = pMsg->pCont;
  int32_t                taskId = pRsp->taskId;
  SStreamTask*           pTask = *(SStreamTask**)taosHashGet(pTq->pStreamTasks, &taskId, sizeof(int32_t));
  if (atomic_load_8(&pTask->taskStatus) != TASK_STATUS__NORMAL) {
    return 0;
  }
  streamProcessRecoverRsp(pTask, pRsp);
  return 0;
}

int32_t tqProcessTaskDropReq(STQ* pTq, char* msg, int32_t msgLen) {
  SVDropStreamTaskReq* pReq = (SVDropStreamTaskReq*)msg;

  SStreamTask* pTask = *(SStreamTask**)taosHashGet(pTq->pStreamTasks, &pReq->taskId, sizeof(int32_t));
  atomic_store_8(&pTask->taskStatus, TASK_STATUS__DROPPING);
  // todo
  // clear queue
  // push drop req into queue
  // launch exec to free memory
  // remove from hash
  return 0;

#if 0
  int32_t              code = taosHashRemove(pTq->pStreamTasks, &pReq->taskId, sizeof(int32_t));
  // set status dropping
  ASSERT(code == 0);
  if (code == 0) {
    // sendrsp
  }
  return code;
#endif
}

int32_t tqProcessTaskRetrieveReq(STQ* pTq, SRpcMsg* pMsg) {
  char*              msgStr = pMsg->pCont;
  char*              msgBody = POINTER_SHIFT(msgStr, sizeof(SMsgHead));
  int32_t            msgLen = pMsg->contLen - sizeof(SMsgHead);
  SStreamRetrieveReq req;
  SDecoder           decoder;
  tDecoderInit(&decoder, msgBody, msgLen);
  tDecodeStreamRetrieveReq(&decoder, &req);
  int32_t      taskId = req.dstTaskId;
  SStreamTask* pTask = *(SStreamTask**)taosHashGet(pTq->pStreamTasks, &taskId, sizeof(int32_t));
  if (atomic_load_8(&pTask->taskStatus) != TASK_STATUS__NORMAL) {
    return 0;
  }
  SRpcMsg rsp = {
      .info = pMsg->info,
      .code = 0,
  };
  streamProcessRetrieveReq(pTask, &req, &rsp);
  return 0;
}

int32_t tqProcessTaskRetrieveRsp(STQ* pTq, SRpcMsg* pMsg) {
  //
  return 0;
}