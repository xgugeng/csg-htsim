// -*- c-basic-offset: 4; indent-tabs-mode: nil -*- 
#ifndef EQDSPACKET_H
#define EQDSPACKET_H

#include <list>
#include "network.h"
#include "ecn.h"

// All EQDS packets are subclasses of Packet.
// They incorporate a packet database, to reuse packet objects that are no longer needed.
// Note: you never construct a new EQDS packet directly; 
// rather you use the static method newpkt() which knows to reuse old packets from the database.

#define VALUE_NOT_SET -1

class EqdsBasePacket : public Packet {
public:
    typedef uint64_t seq_t;
    uint16_t _eqsrcid;  // source tunnel ID for the source.
    uint16_t _eqtgtid;  // destination tunnel ID. 
    const static int ACKSIZE=64; 
};

class EqdsDataPacket : public EqdsBasePacket {
    //using Packet::set_route;
public:
    
    enum PacketType {DATA = 0, SPECULATIVE = 1, RTX = 2};
    //typedef enum {_500B,_1KB,_2KB,_4KB} packet_size;   // need to handle arbitrary packet sizes at end of messages
  
    inline static EqdsDataPacket* newpkt(PacketFlow &flow, const Route &route, 
                                         seq_t seqno, mem_b full_size, 
                                         PacketType pkttype, uint64_t pull_target,
                                         uint32_t destination = UINT32_MAX) {
        EqdsDataPacket* p = _packetdb.allocPacket();
        p->set_route(flow, route, full_size, seqno);  // also sets size and seqno
        p->_type = EQDSDATA;
        p->_is_header = false;
        p->_bounced = false;
        p->_epsn = seqno;
        p->_packet_type = pkttype;
        
        p->_eqsrcid = 0;
        p->_eqtgtid = 0;

        p->_pull_target = pull_target;
        p->_syn = false;
        p->_fin = false;
        
        p->_ar = false;
        p->set_dst(destination);

        p->_direction = NONE;
        p->_path_len = route.size();
        p->_trim_hop = UINT32_MAX;
        p->_trim_direction = NONE;

        return p;
    }
  
    virtual inline void strip_payload() {
        Packet::strip_payload(); 
        //only change the IP packet size, not the approximate one in the EQDS header. 
        Packet::_size = ACKSIZE;
        _trim_hop = _nexthop;
        _trim_direction = _direction;
    };

    virtual inline void set_route(const Route &route) {
        if (_trim_hop!=INT32_MAX)
            _trim_hop -= route.size();

        Packet::set_route(route);
    }

    virtual inline void set_route(PacketFlow& flow, const Route &route, int pkt_size, packetid_t id){
        if (_trim_hop!=INT32_MAX)
            _trim_hop -= route.size();

        Packet::set_route(flow,route,pkt_size,id);
    };


    void free() {_packetdb.freePacket(this);}
    virtual ~EqdsDataPacket(){}

    inline seq_t epsn() const {return _epsn;}
    inline mem_b pull_target() {return _pull_target;}
    inline bool retransmitted() const {return _packet_type == RTX;}

    inline PacketType type() const {return _packet_type;}
    inline bool ar() {return _ar;}
    inline bool unordered() {return _unordered;}
    inline bool syn() {return _syn;}
    inline bool fin() {return _fin;}

    inline int32_t trim_hop() const {return _trim_hop;}
    inline packet_direction trim_direction() const {return _trim_direction;}

    inline int32_t path_id() const {if (_pathid!=UINT32_MAX) return _pathid; else return _route->path_id();}

    virtual PktPriority priority() const {
        if (_is_header) {
            return Packet::PRIO_HI;
        } else {
            return _packet_type == SPECULATIVE?PRIO_LO:PRIO_MID;
        }
    }

protected:
    seq_t _epsn;

    mem_b _pull_target;  // in a real implemention we'd handle wrapping, but here just never wrap

    bool _ar;
    bool _unordered;
    bool _syn;
    bool _fin;

    PacketType _packet_type;

    //trim information, need to see if this stays here or goes to separate header.
    int32_t _trim_hop;
    packet_direction _trim_direction;
    static PacketDB<EqdsDataPacket> _packetdb;
};

class EqdsPullPacket : public EqdsBasePacket {
    using Packet::set_route;
public:
    inline static EqdsPullPacket* newpkt(PacketFlow& flow, const route_t& route,seq_t cumack, mem_b pullno, bool rnr,uint32_t destination = UINT32_MAX) {
        EqdsPullPacket* p = _packetdb.allocPacket();
        p->set_route(flow, route, ACKSIZE, 0);

        assert(p->route());

        p->_type = EQDSPULL;
        p->_is_header = true;
        p->_bounced = false;
        p->_cumulative_ack = cumack;
        p->_pullno = pullno;
        p->_path_len = 0;
        p->set_dst(destination);
        p->_direction = NONE;

        p->_eqsrcid = 0;
        p->_eqtgtid = 0;
        p->_rnr = rnr;
        return p;
    }    

    void free() {_packetdb.freePacket(this);}
    inline seq_t cumulative_ack() const {return _cumulative_ack;}
    inline mem_b pullno() const {return _pullno;}
    inline bool is_rnr() const {return _rnr;}

    virtual PktPriority priority() const {return Packet::PRIO_HI;}
  
    virtual ~EqdsPullPacket(){}

protected:
    seq_t _cumulative_ack;
    mem_b _pullno;
    bool _rnr;

    static PacketDB<EqdsPullPacket> _packetdb;
};

class EqdsAckPacket : public EqdsBasePacket {
    using Packet::set_route;
public:
    inline static EqdsAckPacket* newpkt(PacketFlow &flow, const Route &route, 
                                 seq_t cumulative_ack, seq_t ref_ack, mem_b pullno,
                                 uint16_t path_id, bool ecn_marked, uint32_t destination = UINT32_MAX) {
        EqdsAckPacket* p = _packetdb.allocPacket();
        p->set_route(flow,route,ACKSIZE,0);
        p->_type = EQDSACK;
        p->_is_header = true;
        p->_bounced = false;
        p->_ref_ack = ref_ack;

        p->_cumulative_ack = cumulative_ack;
        p->_pullno = pullno;
        p->_ev = path_id;
        p->_direction = NONE;
        p->_sack_bitmap = 0;
        p->_ecn_echo = ecn_marked;
        p->set_dst(destination);
        return p;
    }
  
    void free() {_packetdb.freePacket(this);}
    inline seq_t ref_ack() const {return _ref_ack;}
    inline seq_t cumulative_ack() const {return _cumulative_ack;}
    inline simtime_picosec residency_time() const {return _residency_time;}
    inline void set_bitmap(uint64_t bitmap){_sack_bitmap = bitmap;};
    inline mem_b pullno() const {return _pullno;}
    uint16_t  ev() const {return _ev;}
    inline bool ecn_echo() const {return _ecn_echo;}
    uint64_t bitmap() const {return _sack_bitmap;}
    virtual PktPriority priority() const {return Packet::PRIO_HI;}
  
    virtual ~EqdsAckPacket(){}

protected:
    seq_t _ref_ack;  // corresponds to the base of the bitmap
    seq_t _cumulative_ack;  // highest in-order packet received.
    mem_b _pullno;
    //seq_t _pullno; I think we need this field too. 

    //SACK bitmap here 
    uint64_t _sack_bitmap;
    uint16_t _ev; //path id for the packet that triggered the SACK 

    bool _rnr;
    bool _ecn_echo;
    simtime_picosec _residency_time;

    static PacketDB<EqdsAckPacket> _packetdb;
};

class EqdsNackPacket : public EqdsBasePacket {
    using Packet::set_route;
public:
    inline static EqdsNackPacket* newpkt(PacketFlow &flow, const Route &route, 
                                  seq_t ref_epsn, mem_b pullno, 
                                  uint16_t path_id,uint32_t destination = UINT32_MAX) {
        EqdsNackPacket* p = _packetdb.allocPacket();
        p->set_route(flow,route,ACKSIZE,ref_epsn);
        p->_type = EQDSNACK;
        p->_is_header = true;
        p->_bounced = false;
        p->_ref_epsn = ref_epsn;
        p->_pullno = pullno;
        p->_ev = path_id; // used to indicate which path the data packet was trimmed on
        p->_ecn_echo = false;
        p->_rnr = false;

        p->_direction = NONE;
        p->_path_len = 0;
        p->set_dst(destination);
        return p;
    }
  
    void free() {_packetdb.freePacket(this);}
    inline seq_t ref_ack() const {return _ref_epsn;}
    inline mem_b pullno() const {return _pullno;}
    uint16_t ev() const {return _ev;}
    inline void set_ecn_echo(bool ecn_echo) {_ecn_echo = ecn_echo;}
    inline bool ecn_echo() const {return _ecn_echo;}
    virtual PktPriority priority() const {return Packet::PRIO_HI;}
  
    virtual ~EqdsNackPacket(){}

protected:
    seq_t _ref_epsn;
    mem_b _pullno;
    uint16_t _ev;
    bool _rnr;
    bool _ecn_echo;
    static PacketDB<EqdsNackPacket> _packetdb;
};

class EqdsRtsPacket : public EqdsDataPacket {
    using Packet::set_route;
public:    
    inline static EqdsRtsPacket* newpkt(PacketFlow& flow, const Route& route, seq_t seqno, seq_t pull_target,bool to,uint32_t destination = UINT32_MAX) {
        EqdsRtsPacket* p = _packetdb.allocPacket();
        p->set_route(flow,route,ACKSIZE,0);
        //p->set_attrs(flow, ACKSIZE, 0);
        p->_type = EQDSRTS;
        p->_is_header = true;
        p->_bounced = false;
        p->_pull_target = pull_target;
        p->_epsn = seqno;
        p->_direction = NONE;    
        p->_to = to;//is this RTS the result of a timeout?

        //this is currently a hack. Needs to be set explicitly by the sender.
        p->_retx_backlog = EqdsDataPacket::data_packet_size();
        p->_ar = true; //always request ack.
        p->set_dst(destination);
        return p;
    }
    
    void free() {_packetdb.freePacket(this);}
    
    inline seq_t retx_backlog() const {return _retx_backlog;}
    inline void set_retx_backlog(seq_t retx_backlog) { _retx_backlog = retx_backlog; }

    inline bool to() const {return _to;}
    inline bool ar() const {return _ar;}

    virtual PktPriority priority() const {return Packet::PRIO_HI;}
    
    virtual ~EqdsRtsPacket(){}

protected:
    seq_t _retx_backlog;
    bool _to;

    static PacketDB<EqdsRtsPacket> _packetdb;
};

#endif
