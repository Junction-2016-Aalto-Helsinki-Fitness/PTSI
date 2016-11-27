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
		this.state = {text1 : '', text2 : '', text3 : ''};
	}
	
	render() {
	return (
      <Image
        source={require('./img/background.jpg')}
        style={styles.container}>
      <View style={{padding: 10}}>
        <Text
          style={styles.teksti}
        >Sign Up</Text>		
        <TextInput
          style={styles.email}
		  placeholder="Email"
          onChangeText={(text1) => this.setState({text1})}
        />
        <TextInput
          style={styles.email}
		  placeholder="Password"
          onChangeText={(text2) => this.setState({text2})}
        />
		<Button
			title="Continue"
			color="grey"
			
		/>		
      </View>
      </Image>
    );
	}
}

const styles = StyleSheet.create({
  container: {
    flex: 1,
    width: undefined,
    height: undefined,
    backgroundColor:'transparent',
    justifyContent: 'center',
    alignItems: 'center',
  },

  email: {
	padding: 10,
	margin: 10,
    height: 40,
    width: 250, 
    justifyContent: 'center',
    alignItems: 'center',
    backgroundColor: 'white',

    borderWidth: 1
  },
  teksti: {
	  fontSize: 36,
	  fontWeight: 'bold',
	  height: 70,
	  width: 250,
	  textAlign: 'center'
  },

});
