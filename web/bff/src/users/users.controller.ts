import { Body, Controller, Delete, Get, Param, Patch, Post } from '@nestjs/common';
import { ApiTags } from '@nestjs/swagger';
import { IsBoolean, IsEmail, IsIn, IsString, MinLength } from 'class-validator';
import { Roles } from '../common/decorators';
import { UsersService } from './users.service';

class CreateUserDto {
  @IsEmail() email!: string;
  @IsString() @MinLength(8) password!: string;
  @IsIn(['admin', 'user']) role!: 'admin' | 'user';
}
class EnabledDto {
  @IsBoolean() enabled!: boolean;
}
class RoleDto {
  @IsIn(['admin', 'user']) role!: 'admin' | 'user';
}
class PasswordDto {
  @IsString() @MinLength(8) password!: string;
}

//! Admin-only user management — the backend of the dashboard's Admin tab.
@ApiTags('admin/users')
@Roles('admin')
@Controller('admin/users')
export class UsersController {
  constructor(private readonly users: UsersService) {}

  @Get()
  list() {
    return this.users.list();
  }

  @Post()
  create(@Body() dto: CreateUserDto) {
    return this.users.create(dto.email, dto.password, dto.role);
  }

  @Patch(':id/enabled')
  setEnabled(@Param('id') id: string, @Body() dto: EnabledDto) {
    return this.users.setEnabled(id, dto.enabled);
  }

  @Patch(':id/role')
  setRole(@Param('id') id: string, @Body() dto: RoleDto) {
    return this.users.setRole(id, dto.role);
  }

  @Patch(':id/password')
  resetPassword(@Param('id') id: string, @Body() dto: PasswordDto) {
    return this.users.resetPassword(id, dto.password);
  }

  @Delete(':id')
  remove(@Param('id') id: string) {
    return this.users.remove(id);
  }
}
