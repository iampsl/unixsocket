package main

import (
	"client/gotocpb"
	"encoding/binary"
	"fmt"
	"log"
	"net"
	"time"

	"github.com/golang/protobuf/proto"
)

func main() {
	conn, err := net.Dial("unix", "/abc")
	if err != nil {
		log.Fatalln(err)
	}
	defer conn.Close()
	beg := time.Now()
	TOTAL := 1000000
	for i := 0; i < TOTAL; i++ {
		var req gotocpb.AddReq
		req.Num1 = 10
		req.Num2 = 108
		err = WriteProcessMsg(conn, gotocpb.CmdID_ADD_REQ, &req)
		if err != nil {
			log.Println(err)
			return
		}
		var rsp gotocpb.AddRsp
		err = ReadProcessMsg(conn, &rsp)
		if err != nil {
			log.Println(err)
			return
		}
		//fmt.Println(rsp.Sum)
	}
	fmt.Println(float64(TOTAL) / (time.Since(beg).Seconds()))
}

//WriteProcessMsg 辅助的发送消息函数
func WriteProcessMsg(conn net.Conn, cmd gotocpb.CmdID, msg proto.Message) error {
	var msgData []byte
	var err error
	if msg != nil {
		msgData, err = proto.Marshal(msg)
		if err != nil {
			return err
		}
	}
	var body gotocpb.MsgBody
	body.ID = cmd
	body.Data = msgData
	data, err := proto.Marshal(&body)
	if err != nil {
		return err
	}
	var head [4]byte
	binary.BigEndian.PutUint32(head[:], uint32(len(data)+4))
	_, err = conn.Write(head[:])
	if err != nil {
		return err
	}
	_, err = conn.Write(data)
	return err
}

//ReadBuffer 读缓冲
var ReadBuffer [1024 * 1024 * 10]byte

//ReadProcessMsg 辅助读消息函数
func ReadProcessMsg(conn net.Conn, msg proto.Message) error {
	ReadSize := 0
	for {
		n, err := conn.Read(ReadBuffer[ReadSize:])
		if err != nil {
			return err
		}
		ReadSize += n
		if ReadSize < 4 {
			continue
		}
		totalLen := binary.BigEndian.Uint32(ReadBuffer[:])
		if totalLen > uint32(len(ReadBuffer)) {
			return fmt.Errorf("msg len(%d) > buffer len(%d)", totalLen, len(ReadBuffer))
		}
		if uint32(ReadSize) < totalLen {
			continue
		}
		break
	}
	var body gotocpb.MsgBody
	err := proto.Unmarshal(ReadBuffer[4:ReadSize], &body)
	if err != nil {
		return err
	}
	return proto.Unmarshal(body.GetData(), msg)
}
