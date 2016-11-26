import React, { Component } from 'react';
import {
  AppRegistry,
  StyleSheet,
  Text,
  View,
  Image,
  TextInput,
  Button
} from 'react-native';


export default class signup extends Component {
    constructor(props){
        super(props);
        this.state = {text1 : '', text2 : ''};
    }
    
    render() {
    return (
      <Image
        source={require('./img/background.jpg')}
        style={styles.container}>
      <View style={{padding: 10}}>
        <TextInput
          style={styles.username}
          placeholder="Username"
          onChangeText={(text1) => this.setState({text1})}/>
        <TextInput
          style={styles.username}
          placeholder="Password"
          onChangeText={(text2) => this.setState({text2})}/>
      </View>
      <Button style={styles.button} title="Login"/>
      </Image>

    );
    }
}

const styles = StyleSheet.create({
  container: {
    flex: 1,
    width: undefined,
    height: undefined,
    backgroundColor: 'transparent',
    justifyContent: 'center',
    alignItems: 'center',

  },

  username: {
    padding: 10,
    margin: 10,
    height: 40, 
    width: 300, 
    justifyContent: 'center',
    alignItems: 'center',
    backgroundColor: 'white'
  },

  keep: {
    color: 'white',
    fontSize: 15,
    paddingLeft: 1,
    margin: 10,
  },

  button: {
    backgroundColor: 'white',
    color: 'black',

  },

});

