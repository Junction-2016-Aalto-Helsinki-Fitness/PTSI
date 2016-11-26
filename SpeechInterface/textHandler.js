import React, { Component } from 'react';
import { AppRegistry, Text, TextInput, View } from 'react-native';

export default class TextHandler extends Component {
  constructor(props) {
    super(props);
    this.state = {text: ''};
  }

  getReply(text) {
    if (text=="hello") {
        return "Hello World!";
    } 
    else {
        return "";
    }
  }

  render() {
    return (
      <View style={{padding: 10}}>
        <TextInput
          style={{height: 40}}
          placeholder="Type here to translate!"
          onChangeText={(text) => this.setState({text})}
        />
        <Text style={{padding: 10, fontSize: 42}}>
          {this.getReply(this.state.text)}
        </Text>
      </View>
    );
  }
}
