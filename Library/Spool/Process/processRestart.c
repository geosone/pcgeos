/*
 * processRestart.c -- the spooler's start and the state file of a detach
 * (the C port of processRestart.asm, and of SaveQueueState of
 * processApp.asm).
 *
 * A detach while jobs are queued writes the state file spoolsta.dat in
 * the spool directory; the jobs' spool files stay.  The next start reads
 * it and queues the jobs again under their IDs, unless GEOS crashed in
 * between; without a state file the spool files of the earlier session
 * are deleted.  The spool???.ps files of print-to-file go either way.
 * Fax files are not built (SpoolNukeFaxFiles).
 *
 * Port32: the asm wrote the PrintQueue block as it was, with nextJobID
 * in LMBH_handle, and read it back the same way.  The C state file has a
 * format of its own, as the block's layout is not the DOS16 one: a
 * header (SpoolStateHeader) and then, queue by queue in the order of the
 * queues, each job's ID, the size of its JobParameters and the
 * JobParameters.  A file of another format (a DOS16 one) is no state
 * file: its spool files are deleted.
 */
#include "spoolLocal.h"
#include <string.h>

#define SPOOL_STATE_FILE	"spoolsta.dat"
#define SPOOL_STATE_MAGIC	0x53505347UL	/* "GSPS" */
#define SPOOL_STATE_VERSION	1

typedef struct {
    dword	SSH_magic;
    word	SSH_version;
    word	SSH_nextJobID;		/* the asm's LMBH_handle */
    word	SSH_numJobs;
    word	SSH_jobParamsSize;	/* sizeof(JobParameters) */
} SpoolStateHeader;

typedef struct {
    word	SSJ_jobID;
    word	SSJ_size;		/* JP_size: the bytes that follow */
} SpoolStateJob;

static void SpoolNukeFiles(const char *pattern)
{
    FileEnumParams p;
    MemHandle mh = 0;
    FileLongName *names;
    word n, i;

    memset(&p, 0, sizeof(p));
    p.FEP_searchFlags = FILE_ENUM_ALL_FILE_TYPES | FESF_CALLBACK;
    p.FEP_returnAttrs = (void *) FESRT_NAME;
    p.FEP_returnSize = sizeof(FileLongName);
    p.FEP_bufSize = FE_BUFSIZE_UNLIMITED;
    p.FEP_callback = (void *) FESC_WILDCARD;
    p.FEP_cbData1 = (uintptr_t) pattern;
    p.FEP_cbData2 = TRUE;
    n = FileEnum(&p, &mh, NULL);
    if (!mh)
	return;
    names = MemLock(mh);
    for (i = 0; i < n; i++)
	FileDelete(names[i]);
    MemFree(mh);
}

/* SpoolCheckSetupFlag: [system] continueSetup */
static Boolean SpoolCheckSetupFlag(void)
{
    Boolean b;

    if (InitFileReadBoolean("system", "continueSetup", &b))
	return FALSE;
    return b;
}

/* ReadAll: size bytes; TRUE if they were not all there */
static Boolean ReadAll(FileHandle fh, void *buf, word size)
{
    return FileRead(fh, buf, size, FALSE) != size;
}

/* ReadJob: the next job of the state file into jp (a block of 64K);
 * TRUE if there is none */
static Boolean ReadJob(FileHandle fh, SpoolStateJob *sj, JobParameters *jp)
{
    if (ReadAll(fh, sj, sizeof(*sj)) ||
	sj->SSJ_size < offsetof(JobParameters, JP_printerData))
	return TRUE;
    memset(jp, 0, sizeof(*jp));
    return ReadAll(fh, jp, sj->SSJ_size);
}

/* SpoolRestartPrinting: the jobs of the state file queued again; TRUE
 * (carry) if none was */
static Boolean SpoolRestartPrinting(FileHandle fh)
{
    SpoolStateHeader h;
    SpoolStateJob sj;
    MemHandle mh;
    JobParameters *jp;
    byte config = (byte) SysGetConfig();
    Boolean queryUser = TRUE, restored = FALSE;
    word i;

    /* not after a crash; in the setup only when restarted, and without
     * asking */
    if (config & SCF_CRASHED)
	return TRUE;
    if (SpoolCheckSetupFlag()) {
	queryUser = FALSE;
	if (!(config & SCF_RESTARTED))
	    return TRUE;
    }

    if (ReadAll(fh, &h, sizeof(h)) || h.SSH_magic != SPOOL_STATE_MAGIC ||
	h.SSH_version != SPOOL_STATE_VERSION ||
	h.SSH_jobParamsSize != sizeof(JobParameters))
	return TRUE;
    nextJobID = h.SSH_nextJobID;
    if (!h.SSH_numJobs)
	return TRUE;

    /* the user is asked once, if a job is left after the canceled ones
     * (StripCanceledJobs): the file is read twice */
    mh = MemAlloc(0xffff, HF_DYNAMIC, HAF_LOCK);
    if (!mh)
	return TRUE;
    jp = MemDeref(mh);
    for (i = 0; i < h.SSH_numJobs; i++) {
	if (ReadJob(fh, &sj, jp))
	    break;
	if (!(jp->JP_spoolOpts & SO_SHUTDOWN_ACTION)) {
	    restored = TRUE;
	    break;
	}
    }
    if (restored && queryUser &&
	SpoolErrorBox(SERROR_PRINT_ON_STARTUP, 0) != SPOOL_IC_YES)
	restored = FALSE;
    if (restored) {
	FilePos(fh, sizeof(h), FILE_POS_START);
	for (i = 0; i < h.SSH_numJobs; i++) {
	    if (ReadJob(fh, &sj, jp))
		break;
	    jp->JP_size = sj.SSJ_size;
	    if (!(jp->JP_spoolOpts & SO_SHUTDOWN_ACTION))
		SpoolAddJobInternal(jp, sj.SSJ_jobID);
	}
    }
    MemFree(mh);
    return !restored;
}

void SpoolBirth(void)
{
    FileHandle fh;
    Boolean restarted = FALSE;

    FilePushDir();
    FileSetStandardPath(SP_SPOOL);
    fh = FileOpen(SPOOL_STATE_FILE, FILE_DENY_RW | FILE_ACCESS_R);
    if (fh) {
	restarted = !SpoolRestartPrinting(fh);
	FileClose(fh, TRUE);
	FileDelete(SPOOL_STATE_FILE);
    }
    /* no state file: GEOS went down without a detach */
    if (!restarted)
	SpoolNukeFiles("spool???.dat");
    SpoolNukeFiles("spool???.ps");
    FilePopDir();
}

/* SaveQueueState: the queued jobs into the state file, for the next
 * start ("kind of like a last will"); the queue is locked */
void SaveQueueState(void)
{
    SpoolStateHeader h;
    SpoolStateJob sj;
    FileHandle fh;
    ChunkHandle q, job;
    JobInfoStruct *j;

    FilePushDir();
    FileSetStandardPath(SP_SPOOL);
    FileDelete(SPOOL_STATE_FILE);
    fh = FileCreate(SPOOL_STATE_FILE, FCF_NATIVE | FILE_CREATE_TRUNCATE |
		    FILE_ACCESS_W | FILE_DENY_W, 0);
    if (!fh) {
	FilePopDir();
	return;
    }
    h.SSH_magic = SPOOL_STATE_MAGIC;
    h.SSH_version = SPOOL_STATE_VERSION;
    h.SSH_nextJobID = nextJobID;
    h.SSH_numJobs = 0;
    h.SSH_jobParamsSize = sizeof(JobParameters);
    FileWrite(fh, &h, sizeof(h), FALSE);
    for (q = PQ()->PQ_firstQueue; q; q = QUEUE(q)->QI_next)
	for (job = QUEUE(q)->QI_curJob; job; job = JOB(job)->JIS_next) {
	    j = JOB(job);
	    sj.SSJ_jobID = j->JIS_jobID;
	    sj.SSJ_size = j->JIS_info.JP_size;
	    FileWrite(fh, &sj, sizeof(sj), FALSE);
	    FileWrite(fh, &JOB(job)->JIS_info, sj.SSJ_size, FALSE);
	    h.SSH_numJobs++;
	}
    FilePos(fh, 0, FILE_POS_START);
    FileWrite(fh, &h, sizeof(h), FALSE);
    FileClose(fh, FALSE);
    FilePopDir();
}
