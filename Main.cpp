#include "MainLoop.h"
#include "Platform.h"
#include "WorkerThread.h"
#include "AsyncThread.h"
#include "SyncQueue.h"
#include "Log.h"

static bool platform_init()
{
    // TODO
    return true;
}

static void platform_deinit()
{
    // TODO
}

/**
 * Simple Example Code
 * How to use my base code.
 *     - MainLoop - Timer
 *     - WorkerThread
 */
struct Msg
{
    int mWhat;
    int mArg1;
    int mArg2;
    int mArg3;

    Msg(int what = 0, int arg1 = 0, int arg2 = 0, int arg3 = 0)
        : mWhat(what), mArg1(arg1), mArg2(arg2), mArg3(arg3) { }
};

class SampleService
{
public:
    explicit SampleService() : mThread("SampleService")
    {
    }

    ~SampleService()
    {
        stop();
    }

    bool start()
    {
        mMsgQ.setEOS(false);
        return mThread.start([this](AsyncThread::Context& ctx) {
            while(ctx.shouldRun())
            {
                Msg msg;
                if (!mMsgQ.get(&msg))
                    break;

                LOGD("Msg : %d", msg.mWhat);
            }
        });
    }

    void stop()
    {
        mMsgQ.setEOS(true);
        mThread.stop();
    }

    void request(int what)
    {
        mMsgQ.put(Msg(what));
    }

private:
    SyncQueue<Msg, 64> mMsgQ;
    AsyncThread mThread;
};

class App : public IWorker, public ITimerHandler
{
public:
    explicit App(MainLoop& loop) : mLoop(loop)
                                 , mTimer(loop.createTimer())
    {
        mTimer.setHandler(this);
    }

    virtual ~App() override
    {
        stop();

        mTimer.setHandler(nullptr);
    }

    bool start()
    {
	    mTimer.start(1000, true);
        mService.start();
        return mThread.start(*this);
    }

    void stop()
    {
        mThread.stop();
        mService.stop();
        mTimer.stop();
    }

private:
    void run() noexcept override
    {
    __TRACE__
        int n = 0;
        while (mThread.shouldRun())
        {
            mThread.msleep(1000);
            mService.request(n++);
        }
    }

    bool onTimerExpired(const ITimer& timer) noexcept override
    {
        mLoop.post([this] {
            LOGD("Timer expired.");
        });
        return true;
    }

private:
    MainLoop& mLoop;

    Timer mTimer;
    WorkerThread mThread;
    SampleService mService;
};

int main()
{
    MainLoop loop;

    Platform platform(loop);
    if (!platform.init(platform_init, platform_deinit))
       return -1;

    App app(loop);
    if (!app.start())
       return -2;

    loop.loop();

    app.stop();

    return 0;
}
