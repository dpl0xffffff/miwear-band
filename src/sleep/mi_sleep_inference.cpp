#include "miwear/sleep/mi_sleep_stage.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef MIWEAR_HAVE_TFLITE

#include "tensorflow/lite/c/c_api.h"

struct MiwearSleepTfliteModel {
    TfLiteModel *model;
    TfLiteInterpreter *interpreter;
};

extern "C" MiwearSleepTfliteModel *miwear_sleep_tflite_load(const char *model_path) {
    if (model_path == NULL) {
        return NULL;
    }
    MiwearSleepTfliteModel *ctx = new MiwearSleepTfliteModel();
    ctx->model = TfLiteModelCreateFromFile(model_path);
    if (ctx->model == NULL) {
        delete ctx;
        return NULL;
    }
    TfLiteInterpreterOptions *opts = TfLiteInterpreterOptionsCreate();
    ctx->interpreter = TfLiteInterpreterCreate(ctx->model, opts);
    TfLiteInterpreterOptionsDelete(opts);
    if (ctx->interpreter == NULL) {
        TfLiteModelDelete(ctx->model);
        delete ctx;
        return NULL;
    }
    if (TfLiteInterpreterAllocateTensors(ctx->interpreter) != kTfLiteOk) {
        TfLiteInterpreterDelete(ctx->interpreter);
        TfLiteModelDelete(ctx->model);
        delete ctx;
        return NULL;
    }
    return ctx;
}

extern "C" void miwear_sleep_tflite_unload(MiwearSleepTfliteModel *ctx) {
    if (ctx == NULL) {
        return;
    }
    if (ctx->interpreter != NULL) {
        TfLiteInterpreterDelete(ctx->interpreter);
    }
    if (ctx->model != NULL) {
        TfLiteModelDelete(ctx->model);
    }
    delete ctx;
}

extern "C" int miwear_sleep_tflite_classify_epochs(MiwearSleepTfliteModel *ctx,
                                                   const float *features,
                                                   size_t epoch_count,
                                                   float *pred_out,
                                                   size_t pred_stride) {
    if (ctx == NULL || ctx->interpreter == NULL || features == NULL || pred_out == NULL || epoch_count == 0) {
        return -1;
    }
    if (epoch_count > MI_SLEEP_MAX_EPOCHS) {
        return -1;
    }
    TfLiteTensor *input = TfLiteInterpreterGetInputTensor(ctx->interpreter, 0);
    if (input == NULL || TfLiteTensorByteSize(input) < MI_SLEEP_MAX_EPOCHS * MI_SLEEP_FEATURE_DIM * sizeof(float)) {
        return -1;
    }
    float *in_buf = static_cast<float *>(TfLiteTensorData(input));
    if (in_buf == NULL) {
        return -1;
    }
    memset(in_buf, 0, MI_SLEEP_MAX_EPOCHS * MI_SLEEP_FEATURE_DIM * sizeof(float));
    memcpy(in_buf, features, epoch_count * MI_SLEEP_FEATURE_DIM * sizeof(float));
    if (TfLiteInterpreterInvoke(ctx->interpreter) != kTfLiteOk) {
        return -1;
    }
    const TfLiteTensor *output = TfLiteInterpreterGetOutputTensor(ctx->interpreter, 0);
    if (output == NULL) {
        return -1;
    }
    const float *out_buf = static_cast<const float *>(TfLiteTensorData(output));
    if (out_buf == NULL) {
        return -1;
    }
    for (size_t i = 0; i < epoch_count; i++) {
        memcpy(pred_out + i * pred_stride, out_buf + i * 4, 4 * sizeof(float));
    }
    return (int)epoch_count;
}

#else

typedef struct MiwearSleepTfliteModel MiwearSleepTfliteModel;

extern "C" MiwearSleepTfliteModel *miwear_sleep_tflite_load(const char *model_path) {
    (void)model_path;
    return NULL;
}

extern "C" void miwear_sleep_tflite_unload(MiwearSleepTfliteModel *ctx) {
    (void)ctx;
}

extern "C" int miwear_sleep_tflite_classify_epochs(MiwearSleepTfliteModel *ctx,
                                                   const float *features,
                                                   size_t epoch_count,
                                                   float *pred_out,
                                                   size_t pred_stride) {
    (void)ctx;
    (void)features;
    (void)epoch_count;
    (void)pred_out;
    (void)pred_stride;
    return -1;
}

#endif
