/*
 * stream.c -- the stream driver (stream.geo) in C, beside stream.asm.
 *
 * A stream is a circular buffer in a block of its own with a reader and
 * a writer side (CInclude/Internal/strDrInt.h has the structure, as
 * strDrInt.def had it); each side has a counting semaphore (bytes to
 * read, room to write), a lock for one thread at a time, an error and
 * a data notifier with a threshold.  Other drivers (the sound library)
 * work on the structure directly and call StreamNotify,
 * StreamWriteDataNotify, StreamReadDataNotify, StreamShutdown and
 * StreamFree: the driver is a library for them (stream32.gp).
 *
 * Port32 (Port32/ABI.md section 7.8):
 *   - the token of a stream is the handle of its block (the asm: the
 *     segment, a virtual segment if movable); SSD_ptr and SD_max stay
 *     offsets in the block;
 *   - the semaphores are the kernel's (Internal/semInt.h), the asm's
 *     interrupts off ghal_crit; SD_closing is a semaphore the destroyer
 *     waits on (the asm's queue, woken with ThreadWakeUpQueue);
 *   - notification routines are C functions (streamC.h's
 *     StreamGeneralNotifyRoutine, StreamSpecialDataNotifyRoutine), the
 *     message carries StreamNotifyMessageParams;
 *   - DR_STREAM_SET_NOTIFY takes the routine in DR_farPtr, an optr in
 *     DR_cx:DR_dx.
 */
#include <geos.h>
#include <driver.h>
#include <geosdriver.h>
#include <heap.h>
#include <object.h>
#include <ghal.h>
#include <string.h>
#include <Internal/strDrInt.h>

#define STREAM_MAX_STREAM_SIZE	32767

/* the block's data, locked if it is movable (MemLockFixedOrMovable) */
static StreamData *Lock(StreamToken token)
{
    return (StreamData *) MemLock((MemHandle) token);
}

static void Unlock(StreamToken token)
{
    MemUnlock((MemHandle) token);
}

static StreamSideData *SideOf(StreamData *sd, sword side)
{
    /* STREAM_READ (-1) has SNT_READER set, STREAM_WRITE (0) not */
    return (side & SNT_READER) ? &sd->SD_reader : &sd->SD_writer;
}

static word SideFlag(StreamData *sd, StreamSideData *s)
{
    return s == &sd->SD_writer ? (word) STREAM_WRITE : (word) STREAM_READ;
}

/* SSD_ptr one byte on (StreamUseByte) */
static void UseByte(StreamData *sd, StreamSideData *s)
{
    if (++s->SSD_ptr == sd->SD_max)
	s->SSD_ptr = SD_DATA_OFFSET;
}

/*
 * StreamNotifyInt: a notifier sent, unless an ack is pending
 */
static void NotifyInt(StreamData *sd, StreamNotifier *n, byte ack,
		      word value, word side)
{
    if (n->SN_ack || n->SN_type == SNM_NONE)
	return;
    n->SN_ack = ack;
    if (n->SN_type == SNM_ROUTINE) {
	((StreamGeneralNotifyRoutine *) n->SN_dest.SND_routine)
	    (n->SN_data, value, side);
    } else {
	StreamNotifyMessageParams p;

	p.SNMP_value = value;
	p.SNMP_stream = (StreamToken) sd->SD_handle;
	p.SNMP_side = side;
	CObjMessageP(MF_FORCE_QUEUE, n->SN_dest.SND_message,
		     (Message) n->SN_data, &p, sizeof(p), NULL, 0);
    }
}

void StreamNotify(StreamData *sd, StreamNotifier *n, byte ack, word value,
		  word side)
{
    NotifyInt(sd, n, ack, value, side);
}

static Boolean WriteByteInt(StreamData *sd, byte b);

/*
 * StreamCheckDataNotify: the data notifier of a side, if it has as
 * much as its threshold; with a threshold of 1 and a routine, the
 * routine takes (reader) or gives (writer) the byte itself
 */
static void CheckDataNotify(StreamData *sd, StreamSideData *s)
{
    word side = SideFlag(sd, s);

    if ((sword) s->SSD_thresh > s->SSD_sem.Sem_value)
	return;
    if (s->SSD_thresh == 1 && s->SSD_data.SN_type == SNM_ROUTINE) {
	StreamSpecialDataNotifyRoutine *r =
	    (StreamSpecialDataNotifyRoutine *) s->SSD_data.SN_dest.SND_routine;

	if (side == (word) STREAM_WRITE) {
	    word v = r(s->SSD_data.SN_data, 0, side);

	    if (v >> 8)
		(void) WriteByteInt(sd, (byte) v);
	} else {
	    word ptr = s->SSD_ptr;
	    byte b = StreamBuffer(sd)[ptr];

	    if (r(s->SSD_data.SN_data, b, side) && ptr == s->SSD_ptr) {
		/* taken: out of the stream */
		UseByte(sd, s);
		ghal_crit_enter();
		s->SSD_sem.Sem_value--;
		ghal_crit_leave();
		KSemV(&sd->SD_writer.SSD_sem);
	    }
	}
	return;
    }
    NotifyInt(sd, &s->SSD_data, STREAM_ACK, (word) s->SSD_sem.Sem_value,
	      side);
}

void StreamWriteDataNotify(StreamData *sd)
{
    CheckDataNotify(sd, &sd->SD_writer);
    sd->SD_state &= ~SS_WDATA;
}

void StreamReadDataNotify(StreamData *sd)
{
    CheckDataNotify(sd, &sd->SD_reader);
    sd->SD_state &= ~SS_RDATA;
}

/* ---------------------------------------------------------------- bytes */

/* StreamGetByte: TRUE if the stream is closing */
static Boolean ReadByteInt(StreamData *sd, byte *b)
{
    (void) KSemP(&sd->SD_reader.SSD_sem, SEM_FOREVER);
    if (sd->SD_state & SS_NUKING)
	return TRUE;
    *b = StreamBuffer(sd)[sd->SD_reader.SSD_ptr];
    UseByte(sd, &sd->SD_reader);
    KSemV(&sd->SD_writer.SSD_sem);
    CheckDataNotify(sd, &sd->SD_writer);
    return FALSE;
}

/* StreamGetByteNB: TRUE if there is none */
static Boolean ReadByteNBInt(StreamData *sd, byte *b)
{
    ghal_crit_enter();
    if (sd->SD_reader.SSD_sem.Sem_value <= 0) {
	ghal_crit_leave();
	return TRUE;
    }
    sd->SD_reader.SSD_sem.Sem_value--;
    *b = StreamBuffer(sd)[sd->SD_reader.SSD_ptr];
    UseByte(sd, &sd->SD_reader);
    ghal_crit_leave();
    KSemV(&sd->SD_writer.SSD_sem);
    CheckDataNotify(sd, &sd->SD_writer);
    return FALSE;
}

/* StreamPutByte: TRUE if the stream is closing */
static Boolean WriteByteInt(StreamData *sd, byte b)
{
    (void) KSemP(&sd->SD_writer.SSD_sem, SEM_FOREVER);
    if (sd->SD_state & (SS_NUKING | SS_LINGERING))
	return TRUE;
    StreamBuffer(sd)[sd->SD_writer.SSD_ptr] = b;
    UseByte(sd, &sd->SD_writer);
    KSemV(&sd->SD_reader.SSD_sem);
    CheckDataNotify(sd, &sd->SD_reader);
    return FALSE;
}

/* StreamPutByteNB: TRUE if there is no room */
static Boolean WriteByteNBInt(StreamData *sd, byte b)
{
    ghal_crit_enter();
    if (sd->SD_writer.SSD_sem.Sem_value <= 0) {
	ghal_crit_leave();
	return TRUE;
    }
    sd->SD_writer.SSD_sem.Sem_value--;
    StreamBuffer(sd)[sd->SD_writer.SSD_ptr] = b;
    UseByte(sd, &sd->SD_writer);
    ghal_crit_leave();
    KSemV(&sd->SD_reader.SSD_sem);
    CheckDataNotify(sd, &sd->SD_reader);
    return FALSE;
}

/* ------------------------------------------------------------ bulk copy */

/* StreamReadBulkCopy: n bytes there, out of the buffer */
static void ReadBulkCopy(StreamData *sd, byte *dst, word n)
{
    byte *buf = StreamBuffer(sd);
    word p = sd->SD_reader.SSD_ptr, first = (word) (sd->SD_max - p);

    if (first > n)
	first = n;
    memcpy(dst, buf + p, first);
    if (n > first) {
	memcpy(dst + first, buf + SD_DATA_OFFSET, (word) (n - first));
	p = (word) (SD_DATA_OFFSET + n - first);
    } else
	p = (word) (p + n);
    if (p == sd->SD_max)
	p = SD_DATA_OFFSET;
    sd->SD_reader.SSD_ptr = p;
    ghal_crit_enter();
    sd->SD_reader.SSD_sem.Sem_value = (sword) (sd->SD_reader.SSD_sem.Sem_value
					       - n);
    KSemAdd(&sd->SD_writer.SSD_sem, (sword) n);
    ghal_crit_leave();
    CheckDataNotify(sd, &sd->SD_writer);
}

/* StreamWriteBulkCopy: room for n bytes there, into the buffer */
static void WriteBulkCopy(StreamData *sd, const byte *src, word n)
{
    byte *buf = StreamBuffer(sd);
    word p = sd->SD_writer.SSD_ptr, first = (word) (sd->SD_max - p);

    if (first > n)
	first = n;
    memcpy(buf + p, src, first);
    if (n > first) {
	memcpy(buf + SD_DATA_OFFSET, src + first, (word) (n - first));
	p = (word) (SD_DATA_OFFSET + n - first);
    } else
	p = (word) (p + n);
    if (p == sd->SD_max)
	p = SD_DATA_OFFSET;
    sd->SD_writer.SSD_ptr = p;
    ghal_crit_enter();
    sd->SD_writer.SSD_sem.Sem_value = (sword) (sd->SD_writer.SSD_sem.Sem_value
					       - n);
    KSemAdd(&sd->SD_reader.SSD_sem, (sword) n);
    ghal_crit_leave();
    CheckDataNotify(sd, &sd->SD_reader);
}

/* ----------------------------------------------------------------- locks */

/* StreamLockWriter: 0, or the error (STREAM_CLOSING, STREAM_WOULD_BLOCK) */
static word LockWriter(StreamData *sd, word block)
{
    if (sd->SD_state & (SS_LINGERING | SS_NUKING))
	return STREAM_CLOSING;
    if (block) {
	(void) KSemP(&sd->SD_writer.SSD_lock, SEM_FOREVER);
	if (sd->SD_state & (SS_LINGERING | SS_NUKING))
	    return STREAM_CLOSING;
	return 0;
    }
    return KSemP(&sd->SD_writer.SSD_lock, 0) == SE_NO_ERROR ? 0 :
	STREAM_WOULD_BLOCK;
}

static word LockReader(StreamData *sd, word block)
{
    if (block) {
	(void) KSemP(&sd->SD_reader.SSD_lock, SEM_FOREVER);
	if (sd->SD_state & SS_NUKING)
	    return STREAM_CLOSING;
	return 0;
    }
    return KSemP(&sd->SD_reader.SSD_lock, 0) == SE_NO_ERROR ? 0 :
	STREAM_WOULD_BLOCK;
}

static void UnlockReader(StreamData *sd)
{
    sd->SD_state &= ~SS_WDATA;
    sd->SD_reader.SSD_data.SN_ack = 0;
    KSemV(&sd->SD_reader.SSD_lock);
}

static void UnlockWriter(StreamData *sd)
{
    sd->SD_state &= ~SS_RDATA;
    sd->SD_writer.SSD_data.SN_ack = 0;
    KSemV(&sd->SD_writer.SSD_lock);
}

/* ------------------------------------------------------ create, destroy */

static Boolean Create(DriverRegs *r)
{
    word size = (word) r->DR_ax;
    HeapFlags hf = (HeapFlags) (r->DR_cx & HF_FIXED);
    MemHandle mh;
    StreamData *sd;

    if (size > STREAM_MAX_STREAM_SIZE) {
	r->DR_ax = STREAM_BUFFER_TOO_LARGE;
	return TRUE;
    }
    if (!size) {
	r->DR_ax = STREAM_CANNOT_ALLOC;
	return TRUE;
    }
    if (!hf)
	hf = HF_SWAPABLE;
    mh = MemAllocSetOwner((GeodeHandle) r->DR_bx,
			  (word) (SD_DATA_OFFSET + size), hf | HF_SHARABLE,
			  HAF_ZERO_INIT);
    if (!mh) {
	r->DR_ax = STREAM_CANNOT_ALLOC;
	return TRUE;
    }
    sd = (StreamData *) MemDeref(mh);
    sd->SD_handle = mh;
    sd->SD_max = (word) (SD_DATA_OFFSET + size);
    KSemInit(&sd->SD_writer.SSD_sem, (sword) size);
    KSemInit(&sd->SD_reader.SSD_sem, 0);
    sd->SD_reader.SSD_thresh = sd->SD_writer.SSD_thresh = 1;
    sd->SD_useCount = 0;
    sd->SD_unbalanced = 0;
    KSemInit(&sd->SD_reader.SSD_lock, 1);
    KSemInit(&sd->SD_writer.SSD_lock, 1);
    KSemInit(&sd->SD_closing, 0);
    sd->SD_reader.SSD_ptr = sd->SD_writer.SSD_ptr = SD_DATA_OFFSET;
    r->DR_bx = mh;
    return FALSE;
}

void StreamFree(StreamToken stream)
{
    MemFree((MemHandle) stream);
}

/*
 * StreamShutdown: access revoked, everyone blocked woken; with
 * STREAM_LINGER first waits for the data to be read
 */
Boolean StreamShutdown(StreamToken stream, sword linger)
{
    StreamData *sd = StreamDataOf(stream);

    ghal_crit_enter();
    if (!linger) {
	if (sd->SD_state & SS_LINGERING) {
	    /* someone lingers: wake it, it nukes */
	    KSemV(&sd->SD_closing);
	    ghal_crit_leave();
	    return TRUE;
	}
    } else if (sd->SD_reader.SSD_sem.Sem_value > 0) {
	sd->SD_state |= SS_LINGERING;
	ghal_crit_leave();
	/* the reader told of all there is */
	sd->SD_reader.SSD_thresh = (word) sd->SD_reader.SSD_sem.Sem_value;
	CheckDataNotify(sd, &sd->SD_reader);
	(void) KSemP(&sd->SD_closing, SEM_FOREVER);
	ghal_crit_enter();
    }
    sd->SD_state |= SS_NUKING;
    if (sd->SD_useCount) {
	KSemVAll(&sd->SD_reader.SSD_lock);
	KSemVAll(&sd->SD_writer.SSD_lock);
	KSemVAll(&sd->SD_reader.SSD_sem);
	KSemVAll(&sd->SD_writer.SSD_sem);
	ghal_crit_leave();
	(void) KSemP(&sd->SD_closing, SEM_FOREVER);
	return FALSE;
    }
    ghal_crit_leave();
    return FALSE;
}

/* ------------------------------------------------------------- notifiers */

static void SetNotify(StreamData *sd, DriverRegs *r)
{
    word type = (word) r->DR_ax;
    StreamSideData *s = SideOf(sd, (sword) ((type & SNT_READER) ? -1 : 0));
    word event = (type & SNT_EVENT) >> SNT_EVENT_OFFSET;
    StreamNotifier *n = event == SNE_DATA ? &s->SSD_data : &s->SSD_error;

    n->SN_type = (byte) (type & SNT_HOW);
    n->SN_data = (word) r->DR_bp;
    if (n->SN_type == SNM_MESSAGE)
	n->SN_dest.SND_message = ConstructOptr((Handle) r->DR_cx,
					       (ChunkHandle) r->DR_dx);
    else
	n->SN_dest.SND_routine = r->DR_farPtr;
    n->SN_ack = 0;
    if (event == SNE_DATA) {
	byte state = sd->SD_state;	/* kept over the notification */

	CheckDataNotify(sd, s);
	sd->SD_state = state;
    } else if (s->SSD_lastErr)
	NotifyInt(sd, n, STREAM_NOACK, s->SSD_lastErr, SideFlag(sd, s));
}

/* ------------------------------------------------------------ read, write */

static Boolean Read(StreamData *sd, DriverRegs *r)
{
    word block = (word) r->DR_ax, want = (word) r->DR_cx, got = 0, err;
    byte *dst = (byte *) r->DR_dsSi;
    Boolean special;

    if (!want)
	return FALSE;
    if ((err = LockReader(sd, block)) != 0) {
	r->DR_ax = err;
	r->DR_cx = 0;
	return TRUE;
    }
    special = sd->SD_writer.SSD_data.SN_type == SNM_ROUTINE &&
	sd->SD_writer.SSD_thresh == 1;
    if (!special) {
	word have;

	ghal_crit_enter();
	have = sd->SD_reader.SSD_sem.Sem_value > 0 ?
	    (word) sd->SD_reader.SSD_sem.Sem_value : 0;
	ghal_crit_leave();
	if (have >= want) {
	    ReadBulkCopy(sd, dst, want);
	    UnlockReader(sd);
	    return FALSE;
	}
	if (have)
	    ReadBulkCopy(sd, dst, have);
	got = have;
	if (!block) {			/* a short read is no error */
	    r->DR_cx = got;
	    UnlockReader(sd);
	    return FALSE;
	}
    }
    /* byte by byte (blocking), or for the special notifier */
    while (got < want) {
	if (block) {
	    /* going away: short, unless lingering with data left */
	    if ((sd->SD_state & (SS_NUKING | SS_LINGERING)) &&
		(!(sd->SD_state & SS_LINGERING) ||
		 sd->SD_reader.SSD_sem.Sem_value <= 0))
		break;
	    if (ReadByteInt(sd, dst + got))
		break;
	} else if (ReadByteNBInt(sd, dst + got))
	    break;
	got++;
    }
    r->DR_cx = got;
    UnlockReader(sd);
    if (got < want) {
	r->DR_ax = STREAM_SHORT_READ_WRITE;
	return TRUE;
    }
    return FALSE;
}

static Boolean Write(StreamData *sd, DriverRegs *r)
{
    word block = (word) r->DR_ax, want = (word) r->DR_cx, put = 0, err;
    const byte *src = (const byte *) r->DR_dsSi;
    Boolean special;

    if (!want)
	return FALSE;
    if ((err = LockWriter(sd, block)) != 0) {
	r->DR_ax = err;
	r->DR_cx = 0;
	return TRUE;
    }
    special = sd->SD_reader.SSD_data.SN_type == SNM_ROUTINE &&
	sd->SD_reader.SSD_thresh == 1;
    if (!special) {
	word room;

	ghal_crit_enter();
	room = sd->SD_writer.SSD_sem.Sem_value > 0 ?
	    (word) sd->SD_writer.SSD_sem.Sem_value : 0;
	ghal_crit_leave();
	if (room >= want) {
	    WriteBulkCopy(sd, src, want);
	    UnlockWriter(sd);
	    return FALSE;
	}
	if (room)
	    WriteBulkCopy(sd, src, room);
	put = room;
	if (!block) {
	    r->DR_cx = put;
	    UnlockWriter(sd);
	    return FALSE;
	}
    }
    while (put < want) {
	if (block) {
	    if ((sd->SD_state & SS_NUKING) || WriteByteInt(sd, src[put]))
		break;
	} else if (WriteByteNBInt(sd, src[put]))
	    break;
	put++;
    }
    r->DR_cx = put;
    UnlockWriter(sd);
    if (put < want) {
	r->DR_ax = STREAM_SHORT_READ_WRITE;
	return TRUE;
    }
    return FALSE;
}

/* -------------------------------------------------------------- strategy */

static Boolean Function(StreamData *sd, word function, DriverRegs *r)
{
    StreamSideData *s;

    switch (function) {
    case DR_STREAM_SET_NOTIFY:
	SetNotify(sd, r);
	return FALSE;
    case DR_STREAM_GET_ERROR:
	s = SideOf(sd, (sword) r->DR_ax);
	r->DR_ax = s->SSD_lastErr;
	s->SSD_lastErr = 0;
	return FALSE;
    case DR_STREAM_SET_ERROR:
	/* stored for the other side, which is told */
	s = SideOf(sd, (sword) ~r->DR_ax);
	s->SSD_lastErr = (word) r->DR_cx;
	NotifyInt(sd, &s->SSD_error, STREAM_NOACK, (word) r->DR_cx,
		  SideFlag(sd, s));
	return FALSE;
    case DR_STREAM_FLUSH: {
	sword n;

	ghal_crit_enter();
	n = sd->SD_reader.SSD_sem.Sem_value;
	if (n <= 0) {
	    ghal_crit_leave();
	    return FALSE;
	}
	sd->SD_reader.SSD_sem.Sem_value = 0;
	sd->SD_reader.SSD_ptr = (word) (sd->SD_reader.SSD_ptr + n);
	if (sd->SD_reader.SSD_ptr >= sd->SD_max)
	    sd->SD_reader.SSD_ptr = (word) (sd->SD_reader.SSD_ptr - sd->SD_max +
					    SD_DATA_OFFSET);
	KSemAdd(&sd->SD_writer.SSD_sem, n);
	ghal_crit_leave();
	CheckDataNotify(sd, &sd->SD_writer);
	return FALSE;
    }
    case DR_STREAM_SET_THRESHOLD: {
	byte state = sd->SD_state;

	s = SideOf(sd, (sword) r->DR_ax);
	s->SSD_thresh = (word) r->DR_cx;
	CheckDataNotify(sd, s);
	sd->SD_state = state;
	return FALSE;
    }
    case DR_STREAM_READ_BYTE: {
	word err = LockReader(sd, (word) r->DR_ax);
	byte b = 0;
	Boolean fail;

	if (err) {
	    r->DR_ax = err;
	    return TRUE;
	}
	fail = r->DR_ax ? ReadByteInt(sd, &b) : ReadByteNBInt(sd, &b);
	UnlockReader(sd);
	if (fail) {
	    r->DR_ax = r->DR_ax ? STREAM_CLOSING : STREAM_WOULD_BLOCK;
	    return TRUE;
	}
	r->DR_ax = b;
	return FALSE;
    }
    case DR_STREAM_WRITE_BYTE: {
	word err = LockWriter(sd, (word) r->DR_ax);
	Boolean fail;

	if (err) {
	    r->DR_ax = err;
	    return TRUE;
	}
	fail = r->DR_ax ? WriteByteInt(sd, (byte) r->DR_cx) :
	    WriteByteNBInt(sd, (byte) r->DR_cx);
	UnlockWriter(sd);
	if (fail) {
	    r->DR_ax = r->DR_ax ? STREAM_CLOSING : STREAM_WOULD_BLOCK;
	    return TRUE;
	}
	return FALSE;
    }
    case DR_STREAM_READ:
	return Read(sd, r);
    case DR_STREAM_WRITE:
	return Write(sd, r);
    case DR_STREAM_QUERY:
	s = SideOf(sd, (sword) r->DR_ax);
	r->DR_ax = s->SSD_sem.Sem_value > 0 ? (word) s->SSD_sem.Sem_value : 0;
	return FALSE;
    default:
	return TRUE;
    }
}

Boolean StreamStrategy(word function, DriverRegs *r)
{
    StreamToken token;
    StreamData *sd;
    Boolean carry;

    switch (function) {
    case DR_INIT:
    case DR_EXIT:
    case DR_SUSPEND:
    case DR_UNSUSPEND:
	return FALSE;
    case DR_STREAM_GET_DEVICE_MAP:
	r->DR_ax = 0;			/* no devices here */
	return FALSE;
    case DR_STREAM_CREATE:
	return Create(r);
    case DR_STREAM_DESTROY: {
	sd = StreamDataOf((StreamToken) r->DR_bx);
	if (sd->SD_state & SS_NUKING) {
	    r->DR_ax = STREAM_CLOSING;
	    return TRUE;
	}
	if (StreamShutdown((StreamToken) r->DR_bx, (sword) r->DR_ax)) {
	    r->DR_ax = STREAM_CLOSING;
	    return TRUE;
	}
	StreamFree((StreamToken) r->DR_bx);
	return FALSE;
    }
    case DRV_ESC_QUERY_ESC:
	r->DR_ax = 0;
	return FALSE;
    default:
	break;
    }
    token = (StreamToken) r->DR_bx;
    sd = Lock(token);
    if (!sd) {
	r->DR_ax = STREAM_CLOSED;
	return TRUE;
    }
    if (sd->SD_state & SS_NUKING) {
	Unlock(token);
	r->DR_ax = STREAM_CLOSING;
	return TRUE;
    }
    ghal_crit_enter();
    sd->SD_useCount++;
    ghal_crit_leave();
    carry = Function(sd, function, r);
    ghal_crit_enter();
    if (--sd->SD_useCount == 0 &&
	(sd->SD_state & (SS_NUKING | SS_LINGERING)) &&
	((sd->SD_state & SS_NUKING) || sd->SD_reader.SSD_sem.Sem_value <= 0)) {
	/* the last one out: the destroyer goes on */
	KSemV(&sd->SD_closing);
	ghal_crit_leave();
	return carry;			/* (the stream may be gone) */
    }
    ghal_crit_leave();
    Unlock(token);
    return carry;
}

DriverInfoStruct DriverTable = {
    DRIVER_STRATEGY(StreamStrategy), DA_CHARACTER, DRIVER_TYPE_STREAM
};
