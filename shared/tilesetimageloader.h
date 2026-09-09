#ifndef TILESETIMAGELOADER_H
#define TILESETIMAGELOADER_H

#include "tileset.h"

#include <QImage>
#include <QMutex>
#include <QMutexLocker>
#include <QObject>
#include <QQueue>
#include <QRunnable>
#include <QThread>
#include <QThreadPool>
#include <QWaitCondition>

#include <functional>
#include <memory>
#include <utility>

class TilesetImageLoader : public QObject
{
public:
    using Image = std::shared_ptr<Tiled::Tileset>;
    using Completion = std::function<void(const Image &)>;
    using Progress = std::function<void(int, int)>;

    explicit TilesetImageLoader(QObject *parent = nullptr)
        : QObject(parent)
    {
        mPool.setMaxThreadCount(qBound(1, QThread::idealThreadCount() - 1, 8));
    }

    ~TilesetImageLoader() override
    {
        mPool.waitForDone();
    }

    void enqueue(const Tiled::Tileset *source, Completion completion)
    {
        Q_ASSERT(QThread::currentThread() == thread());
        Request request;
        request.source = source->imageSource();
        request.source2x = source->imageSource2x();
        request.width = source->tileWidth();
        request.height = source->tileHeight();
        request.spacing = source->tileSpacing();
        request.margin = source->margin();
        request.transparent = source->transparentColor();
        request.completion = std::move(completion);
        mRequests.enqueue(std::move(request));
        startRequests();
    }

    void waitForDone(const Progress &progress = Progress())
    {
        Q_ASSERT(QThread::currentThread() == thread());
        const int total = mActive + mRequests.size();
        if (progress)
            progress(0, total);
        while (mActive > 0) {
            deliverOne(true);
            if (progress)
                progress(total - mActive - mRequests.size(), total);
        }
    }

    int workerCount() const { return mPool.maxThreadCount(); }

private:
    struct Request
    {
        QString source;
        QString source2x;
        int width;
        int height;
        int spacing;
        int margin;
        QColor transparent;
        Completion completion;
    };

    struct Result
    {
        Image image;
        Completion completion;
    };

    class Job : public QRunnable
    {
    public:
        Job(TilesetImageLoader *loader, Request request)
            : mLoader(loader), mRequest(std::move(request))
        {
        }

        void run() override
        {
            Image image = std::make_shared<Tiled::Tileset>(
                        QString(), mRequest.width, mRequest.height,
                        mRequest.spacing, mRequest.margin);
            image->setTransparentColor(mRequest.transparent);
            image->setImageSource2x(mRequest.source2x);
            const QString path = mRequest.source2x.isEmpty()
                    ? mRequest.source : mRequest.source2x;
            image->loadFromImage(QImage(path), mRequest.source);
            QMutexLocker lock(&mLoader->mMutex);
            const bool notify = mLoader->mReady.isEmpty();
            mLoader->mReady.enqueue({std::move(image), std::move(mRequest.completion)});
            mLoader->mAvailable.wakeOne();
            if (notify) {
                QMetaObject::invokeMethod(mLoader, [loader = mLoader]() {
                    while (loader->deliverOne(false)) {
                    }
                }, Qt::QueuedConnection);
            }
        }

    private:
        TilesetImageLoader *mLoader;
        Request mRequest;
    };

    void startRequests()
    {
        while (mActive < mPool.maxThreadCount() && !mRequests.isEmpty()) {
            ++mActive;
            mPool.start(new Job(this, mRequests.dequeue()));
        }
    }

    bool deliverOne(bool wait)
    {
        Q_ASSERT(QThread::currentThread() == thread());
        Result result;
        {
            QMutexLocker lock(&mMutex);
            while (wait && mReady.isEmpty())
                mAvailable.wait(&mMutex);
            if (mReady.isEmpty())
                return false;
            result = mReady.dequeue();
        }
        --mActive;
        startRequests();
        result.completion(result.image);
        return true;
    }

    QThreadPool mPool;
    QQueue<Request> mRequests;
    QQueue<Result> mReady;
    QMutex mMutex;
    QWaitCondition mAvailable;
    int mActive = 0;
};

#endif
