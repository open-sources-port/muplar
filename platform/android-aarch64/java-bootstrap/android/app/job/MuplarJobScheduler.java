package android.app.job;

import android.content.Context;
import java.util.Collections;
import java.util.List;

public class MuplarJobScheduler extends JobScheduler {
    private final Context mContext;

    public MuplarJobScheduler(Context context) {
        this.mContext = context;
    }

    @Override
    public int schedule(JobInfo job) {
        return RESULT_SUCCESS;
    }

    @Override
    public int enqueue(JobInfo job, JobWorkItem workItem) {
        return RESULT_SUCCESS;
    }

    @Override
    public void cancel(int jobId) {
    }

    @Override
    public void cancelAll() {
    }

    @Override
    public List<JobInfo> getAllPendingJobs() {
        return Collections.emptyList();
    }

    @Override
    public JobInfo getPendingJob(int jobId) {
        return null;
    }

    @Override
    public JobScheduler forNamespace(String namespace) {
        return this;
    }

    @Override
    public String getNamespace() {
        return null;
    }

    @Override
    public int getPendingJobReason(int jobId) {
        return 0;
    }
}
