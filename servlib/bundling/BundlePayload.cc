
#include <sys/errno.h>
#include "BundlePayload.h"
#include "debug/Debug.h"
#include "util/StringBuffer.h"

size_t BundlePayload::mem_threshold_;
std::string BundlePayload::dir_;
bool BundlePayload::test_no_remove_;

BundlePayload::BundlePayload()
    : location_(DISK), length_(0), file_(NULL), offset_(0)
{
}

void
BundlePayload::init(int bundleid, location_t location)
{
    location_ = location;
    
    if (location_ == DISK) 
	init_disk(bundleid);
    else if (location == MEMORY) 
	init_memory(bundleid);
    else
	PANIC("unsupported bundle-payload location type %d",location_);
}


void
BundlePayload::init_memory(int bundleid)
{
    location_ = MEMORY;
    
}

void
BundlePayload::init_disk(int bundleid)
{
    StringBuffer path("%s/bundle_%d.dat", dir_.c_str(), bundleid);
    file_ = new FileIOClient();
    file_->logpathf("/bundle/payload/%d", bundleid);
    if (file_->open(path.c_str(), O_EXCL | O_CREAT | O_RDWR, S_IRUSR | S_IWUSR) < 0) {
	log_crit("/bundle/payload", "error opening payload file %s: %s",
		 path.c_str(), strerror(errno));
	return;
    }
}

BundlePayload::~BundlePayload()
{
    if (file_) {
        if (!test_no_remove_)
            file_->unlink();
        file_->close();
        delete file_;
        file_ = NULL;
    }
    
}

void
BundlePayload::serialize(SerializeAction* a)
{
    a->process("filename", &fname_);
    a->process("length",   &length_);
}

void
BundlePayload::set_length(size_t length)
{
    length_ = length;
    location_ = (length_ < mem_threshold_) ? MEMORY : DISK;
    data_.reserve(length);
}

void
BundlePayload::set_data(const char* bp, size_t len)
{
    ASSERT(length_ == 0); // can only use this once
    set_length(len);
    append_data(bp, len);
}

void
BundlePayload::append_data(const char* bp, size_t len)
{
    ASSERT(length_ > 0);

    if (location_ == MEMORY) {
        data_.append(bp, len);
    }

    file_->writeall(bp, len);
}

const char*
BundlePayload::read_data(off_t offset, size_t len)
{
    ASSERT(length_ >= (len + offset));
    
    if (location_ == MEMORY) {
        return data_.data() + offset;

    } else {
        // check if we need to seek first
        if (offset != offset_) {
            file_->lseek(offset, SEEK_SET);
        }

        // now read a chunk into data
        data_.reserve(len);
        file_->readall((char*)data_.data(), len);

        // store offset and return bp
        offset_ = offset + len;
        return data_.data();
    }
}
