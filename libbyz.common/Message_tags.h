#ifndef _Message_tags_h
#define _Message_tags_h 1

//
// Each message type is identified by one of the tags in the set below.
//

const short Free_message_tag=0; // Used to mark free message reps.
                                // A valid message may never use this tag.
const short Request_tag=1;
const short Reply_tag=2;
const short Pre_prepare_tag=3;
const short Prepare_tag=4;
const short Commit_tag=5;
const short Checkpoint_tag=6;
const short Status_tag=7;
const short View_change_tag=8;
const short New_view_tag=9;
const short View_change_ack_tag=10;
const short New_key_tag=11;
const short Meta_data_tag=12;
const short Meta_data_d_tag=13;
const short Data_tag=14;
const short Fetch_tag=15;
const short Wrapped_request_tag=18;
const short Propagate_tag=20;
const short Protocol_instance_change_tag=21;

#endif // _Message_tags_h
