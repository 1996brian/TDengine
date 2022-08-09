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

#include "streamInc.h"

int32_t tEncodeStreamTaskRecoverReq(SEncoder* pEncoder, const SStreamTaskRecoverReq* pReq) {
  if (tStartEncode(pEncoder) < 0) return -1;
  if (tEncodeI64(pEncoder, pReq->streamId) < 0) return -1;
  if (tEncodeI32(pEncoder, pReq->taskId) < 0) return -1;
  if (tEncodeI32(pEncoder, pReq->upstreamTaskId) < 0) return -1;
  if (tEncodeI32(pEncoder, pReq->upstreamNodeId) < 0) return -1;
  tEndEncode(pEncoder);
  return pEncoder->pos;
}

int32_t tDecodeStreamTaskRecoverReq(SDecoder* pDecoder, SStreamTaskRecoverReq* pReq) {
  if (tStartDecode(pDecoder) < 0) return -1;
  if (tDecodeI64(pDecoder, &pReq->streamId) < 0) return -1;
  if (tDecodeI32(pDecoder, &pReq->taskId) < 0) return -1;
  if (tDecodeI32(pDecoder, &pReq->upstreamTaskId) < 0) return -1;
  if (tDecodeI32(pDecoder, &pReq->upstreamNodeId) < 0) return -1;
  tEndDecode(pDecoder);
  return 0;
}

int32_t tEncodeStreamTaskRecoverRsp(SEncoder* pEncoder, const SStreamTaskRecoverRsp* pRsp) {
  if (tStartEncode(pEncoder) < 0) return -1;
  if (tEncodeI64(pEncoder, pRsp->streamId) < 0) return -1;
  if (tEncodeI32(pEncoder, pRsp->reqTaskId) < 0) return -1;
  if (tEncodeI32(pEncoder, pRsp->rspTaskId) < 0) return -1;
  if (tEncodeI8(pEncoder, pRsp->inputStatus) < 0) return -1;
  tEndEncode(pEncoder);
  return pEncoder->pos;
}

int32_t tDecodeStreamTaskRecoverRsp(SDecoder* pDecoder, SStreamTaskRecoverRsp* pReq) {
  if (tStartDecode(pDecoder) < 0) return -1;
  if (tDecodeI64(pDecoder, &pReq->streamId) < 0) return -1;
  if (tDecodeI32(pDecoder, &pReq->reqTaskId) < 0) return -1;
  if (tDecodeI32(pDecoder, &pReq->rspTaskId) < 0) return -1;
  if (tDecodeI8(pDecoder, &pReq->inputStatus) < 0) return -1;
  tEndDecode(pDecoder);
  return 0;
}

int32_t tEncodeSMStreamTaskRecoverReq(SEncoder* pEncoder, const SMStreamTaskRecoverReq* pReq) {
  if (tStartEncode(pEncoder) < 0) return -1;
  if (tEncodeI64(pEncoder, pReq->streamId) < 0) return -1;
  if (tEncodeI32(pEncoder, pReq->taskId) < 0) return -1;
  tEndEncode(pEncoder);
  return pEncoder->pos;
}

int32_t tDecodeSMStreamTaskRecoverReq(SDecoder* pDecoder, SMStreamTaskRecoverReq* pReq) {
  if (tStartDecode(pDecoder) < 0) return -1;
  if (tDecodeI64(pDecoder, &pReq->streamId) < 0) return -1;
  if (tDecodeI32(pDecoder, &pReq->taskId) < 0) return -1;
  tEndDecode(pDecoder);
  return 0;
}

int32_t tEncodeSMStreamTaskRecoverRsp(SEncoder* pEncoder, const SMStreamTaskRecoverRsp* pRsp) {
  if (tStartEncode(pEncoder) < 0) return -1;
  if (tEncodeI64(pEncoder, pRsp->streamId) < 0) return -1;
  if (tEncodeI32(pEncoder, pRsp->taskId) < 0) return -1;
  tEndEncode(pEncoder);
  return pEncoder->pos;
}

int32_t tDecodeSMStreamTaskRecoverRsp(SDecoder* pDecoder, SMStreamTaskRecoverRsp* pReq) {
  if (tStartDecode(pDecoder) < 0) return -1;
  if (tDecodeI64(pDecoder, &pReq->streamId) < 0) return -1;
  if (tDecodeI32(pDecoder, &pReq->taskId) < 0) return -1;
  tEndDecode(pDecoder);
  return 0;
}

int32_t tEncodeSStreamCheckpointInfo(SEncoder* pEncoder, const SStreamCheckpointInfo* pCheckpoint) {
  if (tEncodeI32(pEncoder, pCheckpoint->nodeId) < 0) return -1;
  if (tEncodeI32(pEncoder, pCheckpoint->childId) < 0) return -1;
  if (tEncodeI64(pEncoder, pCheckpoint->stateProcessedVer) < 0) return -1;
  return 0;
}

int32_t tDecodeSStreamCheckpointInfo(SDecoder* pDecoder, SStreamCheckpointInfo* pCheckpoint) {
  if (tDecodeI32(pDecoder, &pCheckpoint->nodeId) < 0) return -1;
  if (tDecodeI32(pDecoder, &pCheckpoint->childId) < 0) return -1;
  if (tDecodeI64(pDecoder, &pCheckpoint->stateProcessedVer) < 0) return -1;
  return 0;
}

int32_t tEncodeSStreamMultiVgCheckpointInfo(SEncoder* pEncoder, const SStreamMultiVgCheckpointInfo* pCheckpoint) {
  if (tEncodeI64(pEncoder, pCheckpoint->streamId) < 0) return -1;
  if (tEncodeI64(pEncoder, pCheckpoint->checkTs) < 0) return -1;
  if (tEncodeI32(pEncoder, pCheckpoint->checkpointId) < 0) return -1;
  if (tEncodeI32(pEncoder, pCheckpoint->taskId) < 0) return -1;
  int32_t sz = taosArrayGetSize(pCheckpoint->checkpointVer);
  if (tEncodeI32(pEncoder, sz) < 0) return -1;
  for (int32_t i = 0; i < sz; i++) {
    SStreamCheckpointInfo* pOneVgCkpoint = taosArrayGet(pCheckpoint->checkpointVer, i);
    if (tEncodeSStreamCheckpointInfo(pEncoder, pOneVgCkpoint) < 0) return -1;
  }
  return 0;
}

int32_t tDecodeSStreamMultiVgCheckpointInfo(SDecoder* pDecoder, SStreamMultiVgCheckpointInfo* pCheckpoint) {
  if (tDecodeI64(pDecoder, &pCheckpoint->streamId) < 0) return -1;
  if (tDecodeI64(pDecoder, &pCheckpoint->checkTs) < 0) return -1;
  if (tDecodeI32(pDecoder, &pCheckpoint->checkpointId) < 0) return -1;
  if (tDecodeI32(pDecoder, &pCheckpoint->taskId) < 0) return -1;
  int32_t sz;
  if (tDecodeI32(pDecoder, &sz) < 0) return -1;
  for (int32_t i = 0; i < sz; i++) {
    SStreamCheckpointInfo oneVgCheckpoint;
    if (tDecodeSStreamCheckpointInfo(pDecoder, &oneVgCheckpoint) < 0) return -1;
    taosArrayPush(pCheckpoint->checkpointVer, &oneVgCheckpoint);
  }
  return 0;
}

int32_t streamSaveStateInfo(SStreamMeta* pMeta, SStreamTask* pTask) {
  void* buf = NULL;

  ASSERT(pTask->taskLevel == TASK_LEVEL__SINK);

  SStreamMultiVgCheckpointInfo checkpoint;
  checkpoint.checkpointId = atomic_fetch_add_32(&pTask->nextCheckId, 1);
  checkpoint.checkTs = taosGetTimestampMs();
  checkpoint.streamId = pTask->streamId;
  checkpoint.taskId = pTask->taskId;
  checkpoint.checkpointVer = pTask->checkpointInfo;

  int32_t len;
  int32_t code;
  tEncodeSize(tEncodeSStreamMultiVgCheckpointInfo, &checkpoint, len, code);
  if (code < 0) {
    return -1;
  }

  buf = taosMemoryCalloc(1, len);
  if (buf == NULL) {
    return -1;
  }
  SEncoder encoder;
  tEncoderInit(&encoder, buf, len);
  tEncodeSStreamMultiVgCheckpointInfo(&encoder, &checkpoint);
  tEncoderClear(&encoder);

  SStreamCheckpointKey key = {
      .taskId = pTask->taskId,
      .checkpointId = checkpoint.checkpointId,
  };

  if (tdbTbUpsert(pMeta->pStateDb, &key, sizeof(SStreamCheckpointKey), buf, len, &pMeta->txn) < 0) {
    ASSERT(0);
    goto FAIL;
  }

  int32_t sz = taosArrayGetSize(pTask->checkpointInfo);
  for (int32_t i = 0; i < sz; i++) {
    SStreamCheckpointInfo* pCheck = taosArrayGet(pTask->checkpointInfo, i);
    pCheck->stateSaveVer = pCheck->stateProcessedVer;
  }

  taosMemoryFree(buf);
  return 0;
FAIL:
  if (buf) taosMemoryFree(buf);
  return -1;
  return 0;
}

int32_t streamLoadStateInfo(SStreamMeta* pMeta, SStreamTask* pTask) {
  void*   pVal = NULL;
  int32_t vLen = 0;
  if (tdbTbGet(pMeta->pStateDb, &pTask->taskId, sizeof(void*), &pVal, &vLen) < 0) {
    return -1;
  }
  SDecoder decoder;
  tDecoderInit(&decoder, pVal, vLen);
  SStreamMultiVgCheckpointInfo aggCheckpoint;
  tDecodeSStreamMultiVgCheckpointInfo(&decoder, &aggCheckpoint);
  tDecoderClear(&decoder);

  pTask->nextCheckId = aggCheckpoint.checkpointId + 1;
  pTask->checkpointInfo = aggCheckpoint.checkpointVer;

  return 0;
}

int32_t streamSaveSinkLevel(SStreamMeta* pMeta, SStreamTask* pTask) {
  ASSERT(pTask->taskLevel == TASK_LEVEL__SINK);
  return streamSaveStateInfo(pMeta, pTask);
}

int32_t streamRecoverSinkLevel(SStreamMeta* pMeta, SStreamTask* pTask) {
  ASSERT(pTask->taskLevel == TASK_LEVEL__SINK);
  return streamLoadStateInfo(pMeta, pTask);
}

int32_t streamSaveAggLevel(SStreamMeta* pMeta, SStreamTask* pTask) {
  ASSERT(pTask->taskLevel == TASK_LEVEL__AGG);
  // TODO save and copy state

  // save state info
  if (streamSaveStateInfo(pMeta, pTask) < 0) {
    return -1;
  }
  return 0;
}

int32_t streamFetchSinkStatus(SStreamTask* pTask) {
  ASSERT(pTask->taskLevel != TASK_LEVEL__SINK);
  // set self status to recover_phase1
  // build fetch status msg
  // send fetch msg
  return 0;
}

int32_t streamProcessFetchStatusRsp(SStreamMeta* pMeta, SStreamTask* pTask, void* msg) {
  // if failed, set timer and retry
  // if successful
  // add rsp state to partial recover hash
  // if complete, begin actual recover
  return 0;
}

int32_t streamRecoverAggLevel(SStreamMeta* pMeta, SStreamTask* pTask) {
  ASSERT(pTask->taskLevel == TASK_LEVEL__AGG);
  // recover sink level
  // after all sink level recovered
  // choose suitable state to recover
  return 0;
}

int32_t streamSaveSourceLevel(SStreamMeta* pMeta, SStreamTask* pTask) {
  ASSERT(pTask->taskLevel == TASK_LEVEL__SOURCE);
  // TODO: save and copy state
  return 0;
}

int32_t streamRecoverSourceLevel(SStreamMeta* pMeta, SStreamTask* pTask) {
  ASSERT(pTask->taskLevel == TASK_LEVEL__SOURCE);
  // if totLevel == 3
  // fetch agg state
  // recover from local state to agg state, not send msg
  // recover from agg state to most recent log v1
  // enable input queue, set status recover_phase2
  // recover from v1 to queue msg v2, set status normal

  // if totLevel == 2
  // fetch sink state
  // recover from local state to sink state v1, send msg
  // enable input queue, set status recover_phase2
  // recover from v1 to queue msg v2, set status normal
  return 0;
}

int32_t streamRecoverTask(SStreamTask* pTask) {
  //
  return 0;
}